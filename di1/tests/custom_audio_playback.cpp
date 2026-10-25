// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_audio_playback.hpp"
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <vector>

using namespace crabe::di1;
using namespace crabe::di1::audio_playback_detail;
namespace fs = std::filesystem;
namespace {
unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL %d: %s\n", __LINE__, #value); ++failures; } } while (0)
struct Channel { bool playing{}, paused{}; void* group{}; float volume{}; Vector location{}; };
struct Sound { bool live{}; };
std::array<Channel, 64> channels;
std::array<Sound, 64> sounds;
unsigned nextSound{}, nextChannel{}, created{}, released{}, stopped{}, lastMode{};
unsigned nativeVersion = 0x44414;
int failStage{};
bool parentPaused{}, hasEffects3d{};
float lastMin{}, lastMax{};
std::vector<unsigned> operations;
void* const events = reinterpret_cast<void*>(1);
void* const systemObject = reinterpret_cast<void*>(2);
void* const effectCategory = reinterpret_cast<void*>(3);
void* const dialogCategory = reinterpret_cast<void*>(4);
void* const effectGroup = reinterpret_cast<void*>(5);
void* const dialogGroup = reinterpret_cast<void*>(6);
void* const effects3dCategory = reinterpret_cast<void*>(8);
void* const effects3dGroup = reinterpret_cast<void*>(9);
int __stdcall getSystem(void* event, void** output) { CHECK(event == events); *output = systemObject; return failStage == 1; }
int __stdcall version(void* system, unsigned* output) { CHECK(system == systemObject); *output = nativeVersion; return failStage == 2; }
int __stdcall category(void* event, const char* name, void** output) {
    CHECK(event == events);
    CHECK(!std::strcmp(name, "sfx") || !std::strcmp(name, "dialog") || !std::strcmp(name, "sfx/sfx3d"));
    if (!std::strcmp(name, "sfx/sfx3d")) { *output = hasEffects3d ? effects3dCategory : nullptr; return hasEffects3d ? 0 : 1; }
    *output = !std::strcmp(name, "sfx") ? effectCategory : dialogCategory;
    return failStage == 3;
}
int __stdcall group(void* cat, void** output) {
    *output = cat == effectCategory ? effectGroup : cat == effects3dCategory ? effects3dGroup : dialogGroup; return failStage == 4;
}
int __stdcall sound(void* system, const char* bytes, unsigned mode, SoundInfo* info, void** output) {
    CHECK(system == systemObject && bytes && info->cbsize == 0x88 && info->length == 4);
    for (const auto value : info->unused) CHECK(value == 0);
    CHECK(std::memcmp(bytes, "test", 4) == 0);
    lastMode = mode;
    if (failStage == 5) return 1;
    auto& s = sounds[nextSound++ % sounds.size()]; CHECK(!s.live); s.live = true;
    *output = &s; ++created; operations.push_back(1); return 0;
}
int __stdcall play(void* system, int index, void* sample, int paused, void** output) {
    CHECK(system == systemObject && index == -1 && paused == 1 && static_cast<Sound*>(sample)->live);
    if (failStage == 6) return 1;
    auto& c = channels[nextChannel++ % channels.size()]; CHECK(!c.playing); c = {}; c.playing = true; c.paused = true;
    *output = &c; operations.push_back(2); return 0;
}
int __stdcall setGroup(void* ch, void* value) { static_cast<Channel*>(ch)->group = value; operations.push_back(3); return failStage == 7; }
int __stdcall setPaused(void* ch, int value) {
    auto& c = *static_cast<Channel*>(ch);
    if (!value) CHECK(c.group != nullptr); // A new channel is never audible before routing.
    c.paused = value != 0; operations.push_back(value ? 4 : 5); return failStage == 9;
}
int __stdcall setVolume(void* ch, float value) { static_cast<Channel*>(ch)->volume = value; return failStage == 8; }
int __stdcall setPosition(void* ch, const Vector* value, const Vector* velocity) {
    CHECK(velocity && velocity->x == 0 && velocity->y == 0 && velocity->z == 0);
    static_cast<Channel*>(ch)->location = *value; return failStage == 10;
}
int __stdcall distance(void*, float nearDistance, float farDistance) { lastMin = nearDistance; lastMax = farDistance; return failStage == 11; }
int __stdcall listenerAt(void* system, int index, Vector* position, Vector* velocity, Vector* forward, Vector* up) {
    CHECK(system == systemObject && index == 0 && position && !velocity && !forward && !up);
    *position = {7, 8, 9}; return 0;
}
int __stdcall playing(void* ch, int* value) { *value = static_cast<Channel*>(ch)->playing ? 1 : 0; return failStage == 12; }
int __stdcall stop(void* ch) { static_cast<Channel*>(ch)->playing = false; ++stopped; operations.push_back(6); return 0; }
int __stdcall release(void* sample) { CHECK(static_cast<Sound*>(sample)->live); static_cast<Sound*>(sample)->live = false; ++released; operations.push_back(7); return 0; }
int __stdcall groupPause(void* g,int* value) {*value=g==reinterpret_cast<void*>(7) && parentPaused;return 0;}
int __stdcall parent(void* g,void** value) {*value=(g==effectGroup || g==dialogGroup)?reinterpret_cast<void*>(7):nullptr;return 0;}
Api fakeApi() { return {getSystem, version, category, group, sound, play, setGroup, setPaused, setVolume, setPosition, distance, playing, stop, release,groupPause,parent,listenerAt}; }
std::shared_ptr<const std::string> clip() { return std::make_shared<const std::string>("test"); }

void fakeMixer() {
    Mixer mixer;
    const auto api = fakeApi();
    auto incomplete = api; incomplete.setGroup = nullptr;
    CHECK(!mixer.bind(events, incomplete));
    CHECK(!mixer.play(1, clip(), CustomAudioBus::Effects, 1, nullptr));
    nativeVersion = 0x44413; CHECK(!mixer.bind(events, api)); nativeVersion = 0x44414;
    CHECK(mixer.bind(events, api));
    CHECK(!mixer.suspended());parentPaused=true;CHECK(mixer.suspended());
    CHECK(!mixer.play(1,clip(),CustomAudioBus::Effects,1,nullptr));parentPaused=false;
    CHECK(!mixer.play(0, clip(), CustomAudioBus::Effects, 1, nullptr));
    CHECK(!mixer.play(1, {}, CustomAudioBus::Effects, 1, nullptr));
    CHECK(!mixer.play(1, clip(), CustomAudioBus::Effects, std::numeric_limits<float>::quiet_NaN(), nullptr));
    CHECK(!mixer.play(1, clip(), CustomAudioBus::Effects, 1.1f, nullptr));
    CHECK(!mixer.play(1, clip(), static_cast<CustomAudioBus>(99), 1, nullptr));
    CHECK(mixer.play(10, clip(), CustomAudioBus::Effects, 0.4f, nullptr));
    CHECK(channels[0].group == effectGroup && channels[0].volume == 0.4f && !channels[0].paused);
    CHECK(lastMode == 0x949 && mixer.active() == 1);
    CustomAudioSpace position{{1, 2, 3}, 60, 10000};
    CHECK(mixer.play(20, clip(), CustomAudioBus::Voice, 0.7f, &position));
    // WORLDRELATIVE | LOGROLLOFF 3D, matching the shipped FEV event mode.
    CHECK(channels[1].group == dialogGroup && channels[1].location.z == 3 && lastMode == 0x180951);
    CHECK(lastMin == 60 && lastMax == 10000);
    const float moved[] = {4,5,6};mixer.move(20,moved);CHECK(channels[1].location.x==4 && channels[0].location.x==0);
    float heard[3]{}; CHECK(mixer.listener(heard) && heard[2] == 9);
    auto invalid = position; invalid.minDistance = 0; CHECK(!mixer.play(21, clip(), CustomAudioBus::Effects, 1, &invalid));
    invalid = position; invalid.maxDistance = 1; CHECK(!mixer.play(21, clip(), CustomAudioBus::Effects, 1, &invalid));
    invalid = position; invalid.position[1] = std::numeric_limits<float>::infinity();
    CHECK(!mixer.play(21, clip(), CustomAudioBus::Effects, 1, &invalid));
    CHECK(mixer.voiceActive(20) && !mixer.voiceActive(10));
    CHECK(!mixer.play(20, clip(), CustomAudioBus::Voice, 1, nullptr));
    // A spatial effect uses the shipped sfx/sfx3d child category when defined.
    position.minDistance = 5; hasEffects3d = true;
    CHECK(mixer.play(20, clip(), CustomAudioBus::Effects, 1, &position));
    CHECK(channels[2].group == effects3dGroup && lastMin == 5);
    mixer.stopOwner(20); CHECK(mixer.active() == 1); hasEffects3d = false;
    CHECK(mixer.play(20, clip(), CustomAudioBus::Voice, 0.7f, &position));
    CHECK(mixer.play(20, clip(), CustomAudioBus::Effects, 1, &position));
    CHECK(channels[4].group == effectGroup);
    mixer.pause(true); CHECK(channels[0].paused && channels[3].paused);
    CHECK(!mixer.play(30, clip(), CustomAudioBus::Effects, 1, nullptr)); CHECK(channels[4].paused);
    mixer.pause(false); CHECK(!channels[0].paused && !channels[3].paused && !channels[4].paused);
    mixer.stopOwner(20); CHECK(mixer.active() == 1 && !channels[3].playing && !channels[4].playing && channels[0].playing);
    channels[0].playing = false; mixer.update(); CHECK(mixer.active() == 0);
    const auto count = created;
    for (unsigned i = 0; i < 16; ++i) CHECK(mixer.play(i + 1, clip(), CustomAudioBus::Effects, 1, nullptr));
    CHECK(!mixer.play(100, clip(), CustomAudioBus::Effects, 1, nullptr));
    CHECK(mixer.active() == 16 && created == count + 16);
    mixer.unbind(); CHECK(mixer.active() == 0 && created == released);
    CHECK(operations.size() >= 2 && operations[operations.size() - 2] == 6 && operations.back() == 7);
    for (int stage = 3; stage <= 11; ++stage) {
        failStage = 0; CHECK(mixer.bind(events, api));
        failStage = stage;
        CHECK(!mixer.play(1, clip(), CustomAudioBus::Effects, 1, stage >= 10 ? &position : nullptr));
        CHECK(mixer.active() == 0 && created == released);
        failStage = 0; mixer.unbind();
    }
    CHECK(mixer.bind(events, api));
    auto bytes = clip(); std::weak_ptr<const std::string> retained = bytes;
    CHECK(mixer.play(1, bytes, CustomAudioBus::Effects, 1, nullptr));
    bytes.reset(); CHECK(!retained.expired());
    failStage = 12; mixer.update(); CHECK(mixer.active() == 0 && retained.expired()); failStage = 0;
    mixer.unbind(); CHECK(created == released);
}
void profile() {
    std::vector<unsigned char> image(0x1207894);
    constexpr std::uintptr_t base = 0x400000;
    for (const auto [thunk, slot] : std::array<std::pair<unsigned, unsigned>, 2>{{{0xDBEC09, 0x1207890}, {0xDBEC29, 0x1207888}}}) {
        image[thunk] = 0xFF; image[thunk + 1] = 0x25;
        const auto address = static_cast<unsigned>(base + slot); std::memcpy(image.data() + thunk + 2, &address, 4);
    }
    CHECK(validProfile(image.data(), image.size(), base));
    CHECK(!validProfile(image.data(), image.size() - 1, base));
    CHECK(!validProfile(image.data(), image.size(), 0xFFFFFF00));
    image[0xDBEC09] = 0xE9; CHECK(!validProfile(image.data(), image.size(), base)); image[0xDBEC09] = 0xFF;
    image[0xDBEC2B] ^= 1; CHECK(!validProfile(image.data(), image.size(), base));
}

std::string readFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read test input");
    return {std::istreambuf_iterator<char>(input), {}};
}
void originalProfile(const fs::path& path) {
    const auto file = readFile(path);
    CHECK(file.size() >= sizeof(IMAGE_DOS_HEADER)); if (file.size() < sizeof(IMAGE_DOS_HEADER)) return;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(file.data());
    CHECK(dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0);
    if (dos->e_lfanew < 0 || static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > file.size()) return;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(file.data() + dos->e_lfanew);
    CHECK(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC);
    if (nt->OptionalHeader.SizeOfImage > 128 * 1024 * 1024) throw std::runtime_error("test image too large");
    std::vector<unsigned char> mapped(nt->OptionalHeader.SizeOfImage);
    const auto* sections = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (static_cast<std::uint64_t>(section.PointerToRawData) + section.SizeOfRawData > file.size() ||
            static_cast<std::uint64_t>(section.VirtualAddress) + section.SizeOfRawData > mapped.size()) throw std::runtime_error("invalid test image section");
        std::memcpy(mapped.data() + section.VirtualAddress, file.data() + section.PointerToRawData, section.SizeOfRawData);
    }
    const auto matched = validProfile(mapped.data(), mapped.size(), nt->OptionalHeader.ImageBase);
    CHECK(matched);
    if (matched) std::puts("Original executable: native update/release import-thunk profile matched.");
}

std::string wav() {
    constexpr unsigned samples = 800;
    std::string bytes(44 + samples * 2, '\0');
    const auto word = [&](unsigned at, unsigned value) { std::memcpy(bytes.data() + at, &value, 4); };
    const auto half = [&](unsigned at, unsigned short value) { std::memcpy(bytes.data() + at, &value, 2); };
    std::memcpy(bytes.data(), "RIFF", 4); word(4, static_cast<unsigned>(bytes.size() - 8));
    std::memcpy(bytes.data() + 8, "WAVEfmt ", 8); word(16, 16); half(20, 1); half(22, 1);
    word(24, 8000); word(28, 16000); half(32, 2); half(34, 16); std::memcpy(bytes.data() + 36, "data", 4); word(40, samples * 2);
    for (unsigned i = 0; i < samples; ++i) half(44 + i * 2, static_cast<unsigned short>(static_cast<short>(std::sin(i * 0.3) * 1000)));
    return bytes;
}
template<class T> T exported(HMODULE dll, const char* name) {
    const auto function = reinterpret_cast<T>(GetProcAddress(dll, name));
    if (!function) throw std::runtime_error(std::string("missing test export: ") + name);
    return function;
}
void nativeDecoder(const fs::path& dllPath, const fs::path& sample) {
    const auto dll = LoadLibraryExW(dllPath.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    if (!dll) throw std::runtime_error("cannot load explicitly selected FMOD for no-sound test");
    const auto create = exported<int (__stdcall*)(void**)>(dll, "FMOD_System_Create");
    const auto output = exported<int (__stdcall*)(void*, int)>(dll, "FMOD_System_SetOutput");
    const auto init = exported<int (__stdcall*)(void*, int, unsigned, void*)>(dll, "FMOD_System_Init");
    const auto createSound = exported<decltype(Api::createSound)>(dll, "FMOD_System_CreateSound");
    const auto getVersion = exported<decltype(Api::getVersion)>(dll, "FMOD_System_GetVersion");
    const auto soundRelease = exported<decltype(Api::releaseSound)>(dll, "FMOD_Sound_Release");
    const auto length = exported<int (__stdcall*)(void*, unsigned*, unsigned)>(dll, "FMOD_Sound_GetLength");
    const auto close = exported<int (__stdcall*)(void*)>(dll, "FMOD_System_Release");
    void* system{};
    const auto createdSystem = create(&system); CHECK(createdSystem == 0 && system);
    if (createdSystem || !system) { FreeLibrary(dll); return; }
    unsigned versionValue{}; CHECK(getVersion(system, &versionValue) == 0 && versionValue == 0x44414);
    std::printf("Native FMOD version=%08X\n", versionValue);
    CHECK(output(system, 2) == 0); // FMOD_OUTPUTTYPE_NOSOUND: test never opens speakers.
    const auto initialized = init(system, 16, 0, nullptr); CHECK(initialized == 0);
    if (!initialized) for (const auto& bytes : std::vector<std::string>{wav(), sample.empty() ? wav() : readFile(sample)}) {
        SoundInfo info{}; info.cbsize = sizeof(info); info.length = static_cast<unsigned>(bytes.size());
        void* decoded{};
        const auto result = createSound(system, bytes.data(), 0x949, &info, &decoded);
        CHECK(result == 0 && decoded);
        std::printf("Native no-sound OPENMEMORY: input=%u bytes result=%d EXINFO=%u bytes\n", info.length, result, info.cbsize);
        if (decoded) { unsigned ms{}; CHECK(length(decoded, &ms, 1) == 0 && ms > 0); std::printf("Decoded duration=%u ms\n", ms); CHECK(soundRelease(decoded) == 0); }
    }
    CHECK(close(system) == 0); FreeLibrary(dll);
}
}
int main(int argc, char** argv) {
    try {
        fakeMixer(); profile();
        if (argc >= 3 && !std::strcmp(argv[1], "--profile")) originalProfile(fs::path(argv[2]));
        else if (argc >= 3 && !std::strcmp(argv[1], "--fmod")) nativeDecoder(fs::path(argv[2]), argc >= 4 ? fs::path(argv[3]) : fs::path{});
    } catch (const std::exception& error) { std::printf("EXCEPTION: %s\n", error.what()); ++failures; }
    std::printf("Custom audio playback checks: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
