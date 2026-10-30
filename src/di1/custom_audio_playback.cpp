// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_audio_playback.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <atomic>
#include <string>
#include <vector>
#ifndef LITERALCRAB_AUDIO_PLAYBACK_TESTS
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#endif

namespace crabe::di1 {
namespace audio_playback_detail {
bool Api::complete() const noexcept {
    return getSystem && getVersion && getCategory && getCategoryGroup &&
        createSound && playSound && setGroup && setPaused && setVolume &&
        setPosition && setDistance && isPlaying && stop && releaseSound && groupPaused && parentGroup;
}
bool Mixer::bind(void* events, const Api& api) noexcept {
    if (events && events == events_ && system_) return true;
    unbind();
    if (!events || !api.complete()) return false;
    void* system{};
    unsigned version{};
    if (api.getSystem(events, &system) || !system || api.getVersion(system, &version) || version != 0x00044414)
        return false;
    api_ = api;
    events_ = events;
    system_ = system;
    return true;
}
void Mixer::clear(Voice& voice) noexcept {
    if (voice.channel && api_.stop) api_.stop(voice.channel);
    if (voice.sound && api_.releaseSound) api_.releaseSound(voice.sound);
    voice = {};
}
void Mixer::update() noexcept {
    for (auto& voice : voices_) if (voice.channel) {
        int playing{};
        if (api_.isPlaying(voice.channel, &playing) || !playing) clear(voice);
    }
}
bool Mixer::play(std::uint32_t owner, std::shared_ptr<const std::string> bytes,
    CustomAudioBus bus, float volume, const CustomAudioSpace* space) noexcept {
    if (!system_ || paused_ || suspended() || !owner || !bytes || bytes->empty() || bytes->size() > std::numeric_limits<unsigned>::max() ||
        !std::isfinite(volume) || volume < 0 || volume > 1 ||
        (bus != CustomAudioBus::Effects && bus != CustomAudioBus::Voice)) return false;
    if (space && (!std::all_of(std::begin(space->position), std::end(space->position), [](float v) { return std::isfinite(v); }) ||
        !std::isfinite(space->minDistance) || !std::isfinite(space->maxDistance) ||
        space->minDistance <= 0 || space->maxDistance < space->minDistance)) return false;
    update();
    if (bus == CustomAudioBus::Voice && std::any_of(voices_.begin(), voices_.end(), [&](const Voice& voice) {
        return voice.sound && voice.owner == owner && voice.bus == CustomAudioBus::Voice;
    })) return false;
    const auto slot = std::find_if(voices_.begin(), voices_.end(), [](const Voice& voice) { return !voice.sound; });
    if (slot == voices_.end()) return false;
    void* category{};
    void* group{};
    // Shipped 3D effects use sfx/sfx3d (a child of sfx); older projects may
    // only define sfx, which remains the fallback.
    if (bus == CustomAudioBus::Effects && space && !api_.getCategory(events_, "sfx/sfx3d", &category) && category &&
        (api_.getCategoryGroup(category, &group) || !group)) group = nullptr;
    if (!group && (api_.getCategory(events_, bus == CustomAudioBus::Voice ? "dialog" : "sfx", &category) || !category ||
        api_.getCategoryGroup(category, &group) || !group)) return false;
    SoundInfo info{};
    info.cbsize = sizeof(info);
    info.length = static_cast<unsigned>(bytes->size());
    // LOOP_OFF | 2D/3D | SOFTWARE | CREATESAMPLE | OPENMEMORY, plus the shipped
    // events' WORLDRELATIVE | LOGROLLOFF for 3D. SOFTWARE keeps category
    // grouping independent of hardware-channel availability. OPENMEMORY decodes
    // supplied bytes and never sends the mod path to the game's file callbacks.
    const unsigned mode = 0x1u | (space ? 0x10u | 0x80000u | 0x100000u : 0x8u) | 0x40u | 0x100u | 0x800u;
    Voice pending{};
    pending.owner = owner;
    pending.bus = bus;
    pending.spatial = space!=nullptr;
    pending.bytes = std::move(bytes);
    if (api_.createSound(system_, pending.bytes->data(), mode, &info, &pending.sound) || !pending.sound) {
        clear(pending); return false;
    }
    if (api_.playSound(system_, -1, pending.sound, 1, &pending.channel) || !pending.channel ||
        api_.setGroup(pending.channel, group) || api_.setVolume(pending.channel, volume)) {
        clear(pending); return false;
    }
    if (space) {
        const Vector location{space->position[0], space->position[1], space->position[2]};
        const Vector velocity{};
        if (api_.setPosition(pending.channel, &location, &velocity) ||
            api_.setDistance(pending.channel, space->minDistance, space->maxDistance)) {
            clear(pending); return false;
        }
    }
    if (api_.setPaused(pending.channel, paused_ ? 1 : 0)) { clear(pending); return false; }
    *slot = std::move(pending);
    return true;
}
void Mixer::stopOwner(std::uint32_t owner) noexcept {
    for (auto& voice : voices_) if (voice.owner == owner && voice.sound) clear(voice);
}
void Mixer::stopAll() noexcept { for (auto& voice : voices_) clear(voice); }
void Mixer::pause(bool value) noexcept {
    paused_ = value;
    for (auto& voice : voices_) if (voice.channel && api_.setPaused(voice.channel, value ? 1 : 0)) clear(voice);
}
void Mixer::unbind() noexcept {
    stopAll();
    api_ = {};
    events_ = system_ = nullptr;
    paused_ = false;
}
std::size_t Mixer::active() const noexcept {
    return static_cast<std::size_t>(std::count_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.sound != nullptr; }));
}
bool Mixer::voiceActive(std::uint32_t owner) const noexcept {
    return std::any_of(voices_.begin(),voices_.end(),[&](const auto& v){return v.channel && v.owner==owner && v.bus==CustomAudioBus::Voice;});
}
void Mixer::move(std::uint32_t owner,const float* position) noexcept {
    if (!position || !std::isfinite(position[0]) || !std::isfinite(position[1]) || !std::isfinite(position[2])) return;
    const Vector location{position[0],position[1],position[2]},velocity{};
    for (auto& voice : voices_) if (voice.owner==owner && voice.channel && voice.spatial)
        if (api_.setPosition(voice.channel,&location,&velocity)) clear(voice);
}
bool Mixer::listener(float (&position)[3]) const noexcept {
    Vector location{};
    if (!system_ || !api_.listener || api_.listener(system_, 0, &location, nullptr, nullptr, nullptr) ||
        !std::isfinite(location.x) || !std::isfinite(location.y) || !std::isfinite(location.z)) return false;
    position[0] = location.x; position[1] = location.y; position[2] = location.z;
    return true;
}
bool Mixer::suspended() const noexcept {
    if (!events_ || paused_) return true;
    // Inspect native parent groups too: the game can pause a parent while the
    // leaf category has no local pause flag. Never write a native group flag.
    for (const char* name : {"sfx","dialog"}) {
        void* category{};void* group{};
        if (api_.getCategory(events_,name,&category) || !category ||
            api_.getCategoryGroup(category,&group) || !group) return true;
        for (unsigned depth=0;group && depth<16;++depth) {
            int paused{};void* parent{};
            if (api_.groupPaused(group,&paused) || paused || api_.parentGroup(group,&parent)) return true;
            if (parent==group) return true;
            group=parent;
            if (depth==15 && group) return true;
        }
    }
    return false;
}
bool validProfile(const unsigned char* image, std::size_t bytes, std::uintptr_t base) noexcept {
    constexpr std::array<std::pair<std::size_t, std::size_t>, 2> entries{{{0xDBEC09, 0x1207890}, {0xDBEC29, 0x1207888}}};
    if (!image || base > std::numeric_limits<std::uint32_t>::max()) return false;
    for (const auto [thunk, slot] : entries) {
        if (thunk > bytes || bytes - thunk < 6 || slot > bytes || bytes - slot < 4 ||
            base > std::numeric_limits<std::uint32_t>::max() - slot) return false;
        std::uint32_t actual{};
        std::memcpy(&actual, image + thunk + 2, sizeof(actual));
        if (image[thunk] != 0xFF || image[thunk + 1] != 0x25 || actual != base + slot) return false;
    }
    return true;
}
}

#ifndef LITERALCRAB_AUDIO_PLAYBACK_TESTS
namespace {
using EventCall = int (__stdcall*)(void*);
EventCall originalUpdate{}, originalRelease{};
void* hookTargets[2]{};
unsigned hooksCreated{};
CustomAudioTick tickCallback{};
audio_playback_detail::Mixer mixer;
audio_playback_detail::Api api;
// FMOD Ex 4.44 event C API (F_API = __stdcall); resolved with the mixer's exports.
struct EventDataApi {
    int (__stdcall* project)(void*, int, void**){};          // FMOD_EventSystem_GetProjectByIndex
    int (__stdcall* projectGroups)(void*, int*){};           // FMOD_EventProject_GetNumGroups
    int (__stdcall* projectGroup)(void*, int, int, void**){}; // FMOD_EventProject_GetGroupByIndex
    int (__stdcall* groupGroups)(void*, int*){};             // FMOD_EventGroup_GetNumGroups
    int (__stdcall* groupGroup)(void*, int, int, void**){};   // FMOD_EventGroup_GetGroupByIndex
    int (__stdcall* groupEvent)(void*, const char*, unsigned, void**){}; // FMOD_EventGroup_GetEvent
    int (__stdcall* loadData)(void*, int, unsigned){};       // FMOD_EventGroup_LoadEventData
    int (__stdcall* projectInfo)(void*, void*){};            // FMOD_EventProject_GetInfo (diagnostics)
} eventData;
bool eventDataReady{};
constexpr unsigned eventInfoOnly = 0x4;   // FMOD_EVENT_INFOONLY
constexpr int streamsAndSamples = 0;      // FMOD_EVENT_RESOURCE_STREAMS_AND_SAMPLES
SRWLOCK eventLock = SRWLOCK_INIT;
std::array<std::array<char, 96>, 8> pendingEvents{};
std::size_t pendingCount{};
// Per bound event system: names whose group waves were loaded, and lookups still failing (a bank
// a swap just requested may load a moment after the first play), retried up to a limit.
std::vector<std::string> requestedEvents;
std::vector<std::pair<std::string, unsigned>> missingEvents;
constexpr unsigned eventRetries = 40;
bool exportsReady{}, failureLogged{};
std::atomic<bool> gameSuspended{true};
template<class T> bool symbol(HMODULE module, const char* name, T& value) {
    value = reinterpret_cast<T>(GetProcAddress(module, name));
    return value != nullptr;
}
bool resolveApi() {
    if (exportsReady) return true;
    const auto core = GetModuleHandleW(L"fmodex.dll");
    const auto events = GetModuleHandleW(L"fmod_event.dll");
    if (!core || !events) return false;
    const bool complete =
        symbol(events, "FMOD_EventSystem_GetSystemObject", api.getSystem) &&
        symbol(core, "FMOD_System_GetVersion", api.getVersion) &&
        symbol(events, "FMOD_EventSystem_GetCategory", api.getCategory) &&
        symbol(events, "FMOD_EventCategory_GetChannelGroup", api.getCategoryGroup) &&
        symbol(core, "FMOD_System_CreateSound", api.createSound) &&
        symbol(core, "FMOD_System_PlaySound", api.playSound) &&
        symbol(core, "FMOD_Channel_SetChannelGroup", api.setGroup) &&
        symbol(core, "FMOD_Channel_SetPaused", api.setPaused) &&
        symbol(core, "FMOD_Channel_SetVolume", api.setVolume) &&
        symbol(core, "FMOD_Channel_Set3DAttributes", api.setPosition) &&
        symbol(core, "FMOD_Channel_Set3DMinMaxDistance", api.setDistance) &&
        symbol(core, "FMOD_Channel_IsPlaying", api.isPlaying) &&
        symbol(core, "FMOD_Channel_Stop", api.stop) &&
        symbol(core, "FMOD_Sound_Release", api.releaseSound) &&
        symbol(core, "FMOD_ChannelGroup_GetPaused", api.groupPaused) &&
        symbol(core, "FMOD_ChannelGroup_GetParentGroup", api.parentGroup);
    symbol(core, "FMOD_System_Get3DListenerAttributes", api.listener);
    eventDataReady = symbol(events, "FMOD_EventSystem_GetProjectByIndex", eventData.project) &&
        symbol(events, "FMOD_EventProject_GetNumGroups", eventData.projectGroups) &&
        symbol(events, "FMOD_EventProject_GetGroupByIndex", eventData.projectGroup) &&
        symbol(events, "FMOD_EventGroup_GetNumGroups", eventData.groupGroups) &&
        symbol(events, "FMOD_EventGroup_GetGroupByIndex", eventData.groupGroup) &&
        symbol(events, "FMOD_EventGroup_GetEvent", eventData.groupEvent) &&
        symbol(events, "FMOD_EventGroup_LoadEventData", eventData.loadData);
    symbol(events, "FMOD_EventProject_GetInfo", eventData.projectInfo);
    exportsReady = complete;
    return complete;
}
// Depth-first search of one group tree for the event; loads the owning group's waves.
bool loadEventGroup(void* group, const char* name, int depth) {
    void* event{};
    if (!eventData.groupEvent(group, name, eventInfoOnly, &event) && event)
        return !eventData.loadData(group, streamsAndSamples, 0);
    int count{};
    if (depth > 8 || eventData.groupGroups(group, &count)) return false;
    for (int i = 0; i < count && i < 512; ++i) {
        void* child{};
        if (!eventData.groupGroup(group, i, 0, &child) && child && loadEventGroup(child, name, depth + 1)) return true;
    }
    return false;
}
void loadPendingEvents(void* events) {
    std::array<std::array<char, 96>, 8> names{};
    std::size_t count{};
    AcquireSRWLockExclusive(&eventLock);
    names = pendingEvents; count = pendingCount; pendingCount = 0;
    ReleaseSRWLockExclusive(&eventLock);
    if (!count || !eventDataReady) return;
    for (std::size_t n = 0; n < count; ++n) {
        const char* name = names[n].data();
        if (std::find(requestedEvents.begin(), requestedEvents.end(), name) != requestedEvents.end()) continue;
        auto missing = std::find_if(missingEvents.begin(), missingEvents.end(), [&](const auto& m) { return m.first == name; });
        if (missing != missingEvents.end() && missing->second >= eventRetries) continue;
        bool loaded{};
        std::string projects;
        for (int p = 0; p < 64 && !loaded; ++p) {
            void* project{};
            if (eventData.project(events, p, &project) || !project) break;
            if (eventData.projectInfo && projects.size() < 600) {
                // FMOD_EVENT_PROJECTINFO: int index, then char name[256]; the rest stays zero.
                alignas(8) std::array<char, 4096> info{};
                if (!eventData.projectInfo(project, info.data())) projects += std::string(info.data() + 4, strnlen(info.data() + 4, 255)) + " ";
            }
            int groups{};
            if (eventData.projectGroups(project, &groups)) continue;
            for (int g = 0; g < groups && g < 512 && !loaded; ++g) {
                void* group{};
                if (!eventData.projectGroup(project, g, 0, &group) && group) loaded = loadEventGroup(group, name, 0);
            }
        }
        if (loaded) {
            if (requestedEvents.size() < 256) requestedEvents.emplace_back(name);
            if (missing != missingEvents.end()) missingEvents.erase(missing);
            log("Custom audio: native event %s group waves loaded.", name);
            continue;
        }
        if (missing == missingEvents.end()) {
            if (missingEvents.size() >= 256) continue;
            missingEvents.emplace_back(name, 0u); missing = missingEvents.end() - 1;
        }
        if (++missing->second == 1 || missing->second == eventRetries)
            log("Custom audio: native event %s not in a loaded bank yet (attempt %u; projects: %s).", name, missing->second, projects.c_str());
    }
}
int __stdcall audioUpdate(void* events) {
    const int result = originalUpdate(events);
    const auto error = GetLastError();
    if (!result) {
        if (resolveApi() && mixer.bind(events, api)) {
            loadPendingEvents(events);
            mixer.update();
            gameSuspended.store(mixer.suspended(),std::memory_order_relaxed);
            if (tickCallback) tickCallback();
        } else if (!failureLogged) {
            log("Custom audio: matching native FMOD mixer unavailable; custom playback withheld.");
            failureLogged = true;
        }
    }
    SetLastError(error);
    return result;
}
int __stdcall audioRelease(void* events) {
    gameSuspended.store(true,std::memory_order_relaxed);
    requestedEvents.clear();
    missingEvents.clear();
    if (mixer.events() == events) mixer.unbind();
    return originalRelease(events);
}
}
bool installCustomAudioPlayback(HMODULE host, CustomAudioTick tick) {
    if (hooksCreated) { tickCallback = tick; return true; }
    const auto* image = reinterpret_cast<const unsigned char*>(host);
    if (!readable(image, sizeof(IMAGE_DOS_HEADER))) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (!readable(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
        !readable(image + 0xDBEC09, 0x26) ||
        !audio_playback_detail::validProfile(image, nt->OptionalHeader.SizeOfImage, reinterpret_cast<std::uintptr_t>(host))) {
        log("Custom audio: native FMOD import-thunk profile differs; no hooks installed.");
        return false;
    }
    hookTargets[0] = const_cast<unsigned char*>(image + 0xDBEC09);
    hookTargets[1] = const_cast<unsigned char*>(image + 0xDBEC29);
    void* detours[] = {reinterpret_cast<void*>(&audioUpdate), reinterpret_cast<void*>(&audioRelease)};
    void** originals[] = {reinterpret_cast<void**>(&originalUpdate), reinterpret_cast<void**>(&originalRelease)};
    for (unsigned i = 0; i < 2; ++i) {
        if (MH_CreateHook(hookTargets[i], detours[i], originals[i]) != MH_OK) { removeCustomAudioPlayback(); return false; }
        ++hooksCreated;
    }
    tickCallback = tick;
    for (unsigned i = 0; i < 2; ++i)
        if (MH_EnableHook(hookTargets[i]) != MH_OK) { removeCustomAudioPlayback(); return false; }
    log("Custom audio: native FMOD update/release import thunks enabled; sfx/dialog mixer categories preserved.");
    return true;
}
bool playCustomAudio(std::uint32_t owner, std::shared_ptr<const std::string> bytes,
    CustomAudioBus bus, float volume, const CustomAudioSpace* space) noexcept {
    return mixer.play(owner, std::move(bytes), bus, volume, space);
}
void stopCustomAudio(std::uint32_t owner) noexcept { mixer.stopOwner(owner); }
void requestNativeEventData(const char* event) noexcept {
    if (!event) return;
    std::array<char, 96> name{};
    std::size_t length{};
    while (event[length] && length + 1 < name.size()) { name[length] = event[length]; ++length; }
    if (!length || event[length]) return;
    AcquireSRWLockExclusive(&eventLock);
    bool queued{};
    for (std::size_t i = 0; i < pendingCount; ++i) queued = queued || !std::strcmp(pendingEvents[i].data(), name.data());
    if (!queued && pendingCount < pendingEvents.size()) pendingEvents[pendingCount++] = name;
    ReleaseSRWLockExclusive(&eventLock);
}
void stopAllCustomAudio() noexcept { mixer.stopAll(); }
void setCustomAudioPaused(bool value) noexcept { mixer.pause(value); }
bool customAudioPlaybackSuspended() noexcept {return gameSuspended.load(std::memory_order_relaxed);}
bool customAudioVoiceActive(std::uint32_t owner) noexcept {return mixer.voiceActive(owner);}
void updateCustomAudioPosition(std::uint32_t owner,const float* position) noexcept {mixer.move(owner,position);}
bool customAudioListener(float (&position)[3]) noexcept {return mixer.listener(position);}
void removeCustomAudioPlayback() noexcept {
    tickCallback = nullptr;
    for (unsigned i = 0; i < hooksCreated; ++i) { MH_DisableHook(hookTargets[i]); MH_RemoveHook(hookTargets[i]); }
    hooksCreated = 0;
    mixer.unbind();
    api = {};
    exportsReady = failureLogged = false;
    originalUpdate = originalRelease = nullptr;
    gameSuspended.store(true,std::memory_order_relaxed);
}
#endif
}
