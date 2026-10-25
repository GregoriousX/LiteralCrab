// SPDX-License-Identifier: GPL-3.0-only
// Synthetic loose-audio declarations; no shipped sounds or personal paths.
#include "di1/custom_audio_manifest.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

using namespace crabe::di1;
namespace fs = std::filesystem;
namespace {
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
struct Fixture {
    fs::path root;
    Fixture() {
        root = fs::absolute(fs::temp_directory_path()) / ("literalcrab-audio-manifest-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        if (!fs::create_directory(root)) throw std::runtime_error("test root already exists");
    }
    ~Fixture() {
        if (root.parent_path() == fs::absolute(fs::temp_directory_path()) && root.filename().wstring().starts_with(L"literalcrab-audio-manifest-")) {
            std::error_code error; fs::remove_all(root, error);
        }
    }
};
void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!out) throw std::runtime_error("test fixture write failed");
}
void large(const fs::path& path, std::size_t size) {
    write(path, "RIFFxxxxWAVE");
    std::ofstream out(path, std::ios::binary | std::ios::in | std::ios::out);
    out.seekp(static_cast<std::streamoff>(size - 1)); out.put('\0');
    if (!out) throw std::runtime_error("large fixture write failed");
}
crabe::mods::Mod mod(const fs::path& root, std::string name, std::string_view ini) {
    crabe::mods::Mod out;
    out.folder = out.name = std::move(name); out.path = (root / out.folder).wstring();
    write(fs::path(out.path) / "mod.ini", ini);
    return out;
}
CustomFigure character(const crabe::mods::Mod& m, std::string name = "LCR_Test") {
    CustomFigure f;
    f.modPath = m.path; f.name = std::move(name); f.actor = "lcr_test"; f.sku = 1000999;
    f.gameplay.sourceActor = "av_buzz";
    return f;
}
CustomTool tool(const crabe::mods::Mod& m) {
    CustomTool t;
    t.modPath = m.path; t.table.id = "LCR_TEST_GUN"; t.table.actor = "lcr_tool_lcr_test_gun";
    t.privateDna = "characters/literalcrab/tools/lcr_test_gun.dnax";
    return t;
}
bool logged(const std::vector<std::string>& messages, std::string_view fragment) {
    return std::any_of(messages.begin(), messages.end(), [&](const auto& text) { return text.find(fragment) != text.npos; });
}
const AudioCue* cue(const AudioProfile& p, std::string_view name, bool replace = false) {
    const auto found = std::find_if(p.cues.begin(), p.cues.end(), [&](const auto& c) { return c.trigger == name && c.replace == replace; });
    return found == p.cues.end() ? nullptr : &*found;
}
void declarations(const fs::path& root) {
    const auto m = mod(root, "all", "[Mod]\nName=Audio test\n[Character.Audio]\n"
        "Banks=Zurgbot_Melee,other/native.fev\nVolume=0.7\nChance=0.5\nCooldown=0.2\n"
        "CollisionLanded=sounds/land.wav,event:land_native\nCollisionLanded.Volume=0.4\n"
        "CollisionLanded.Chance=1\nCollisionLanded.Cooldown=0\nReplace.source_footstep=sounds/land.wav\n"
        "MinDistance=7\nCollisionLanded.MinDistance=2\nCollisionLanded.MaxDistance=80\nReplace.source_footstep.Spatial=0\n"
        "[Character.Voice]\nIdleInterval=45\nIdle=sounds/quip.ogg\nSpawn=event:hello\n"
        "[Tool.Audio]\nMunitionFired=sounds/land.wav\n[Tool.Voice]\nSpawn=event:equipped\nSpatial=0\n");
    write(fs::path(m.path) / "sounds/land.wav", "RIFFxxxxWAVE");
    write(fs::path(m.path) / "sounds/quip.ogg", std::string("OggS\0\0\0\0\x01vorbis", 15));
    std::vector<std::string> messages;
    const auto profiles = loadCustomAudio({m}, {character(m)}, {tool(m)}, messages);
    CHECK(profiles.size() == 4 && !logged(messages, "REJECTED"));
    if (profiles.size() != 4) return;
    CHECK(!profiles[0].tool && !profiles[0].voice && profiles[0].sku == 1000999);
    CHECK(profiles[0].identity == "LCR_Test" && profiles[0].dna == "characters/literalcrab/lcr_test.dnax");
    CHECK(profiles[0].marker == "LiteralCrabCustomDNA:LCR_Test");
    CHECK(profiles[0].banks == std::vector<std::string>({"Zurgbot_Melee.fev", "other/native.fev"}));
    const auto* land = cue(profiles[0], "CollisionLanded");
    const auto* replace = cue(profiles[0], "source_footstep", true);
    CHECK(land && replace && land->variants.size() == 2);
    if (land && replace) {
        CHECK(land->volume == 0.4f && land->chance == 1 && land->cooldown == 0);
        CHECK(land->variants[0].bytes && *land->variants[0].bytes == "RIFFxxxxWAVE");
        CHECK(land->variants[1].event == "land_native" && !land->variants[1].bytes);
        CHECK(replace->variants[0].bytes == land->variants[0].bytes);
        CHECK(replace->volume == 0.7f && replace->chance == 0.5f && replace->cooldown == 0.2);
        CHECK(profiles[2].cues[0].variants[0].bytes == land->variants[0].bytes);
        CHECK(land->spatial && land->minDistance == 2 && land->maxDistance == 80);
        CHECK(!replace->spatial && replace->minDistance == 7 && replace->maxDistance == 10000);
    }
    CHECK(profiles[1].voice && profiles[1].idleInterval == 45 && profiles[1].cues[0].cooldown == 15);
    // Native FEV defaults: effects 3D min 5, dialogue 3D min 60, inverse to 10000.
    CHECK(profiles[1].cues[0].spatial && profiles[1].cues[0].minDistance == 60 && profiles[1].cues[0].maxDistance == 10000);
    CHECK(profiles[2].tool && !profiles[2].voice && profiles[2].marker == "LiteralCrabTool:LCR_TEST_GUN");
    CHECK(profiles[2].cues[0].spatial && profiles[2].cues[0].minDistance == 5);
    CHECK(profiles[3].tool && profiles[3].voice && !profiles[3].cues[0].spatial);
}
void ignored(const fs::path& root) {
    const auto m = mod(root, "ignored", "[Character.Audio]\nSpawn=missing.wav\n[Tool.Audio]\nSpawn=missing.ogg\n");
    std::vector<std::string> messages;
    CHECK(loadCustomAudio({m}, {}, {}, messages).empty() && messages.empty());
    auto f = character(m); f.gameplay = {};
    auto t = tool(m); t.privateDna.clear();
    CHECK(loadCustomAudio({m}, {f}, {t}, messages).empty() && messages.empty());
    CHECK(loadCustomAudio({}, {character(m)}, {tool(m)}, messages).empty()); // Disabled mod omitted by Index.
    const auto normal = mod(root, "plain", "[Mod]\nName=No audio\n[Other]\nAnything=unchanged\n");
    CHECK(loadCustomAudio({normal}, {character(normal)}, {}, messages).empty() && messages.empty());
}
void invalid(const fs::path& root) {
    unsigned index{};
    const auto reject = [&](std::string ini, std::string_view reason) {
        const auto m = mod(root, "invalid_" + std::to_string(index++), "[Character.Audio]\n" + ini);
        write(fs::path(m.path) / "ok.wav", "RIFFxxxxWAVE");
        write(fs::path(m.path) / "bad.wav", "not a wav");
        write(fs::path(m.path) / "bad.ogg", "OggS OpusHead");
        const auto f = character(m);
        std::vector<std::string> messages;
        CHECK(loadCustomAudio({m}, {f}, {}, messages).empty());
        CHECK(logged(messages, "REJECTED") && logged(messages, reason));
        CHECK(f.gameplay.sourceActor == "av_buzz");
    };
    reject("Spawn=ok.wav\nspawn=event:other\n", "duplicate audio key");
    reject("Spawn=ok.wav\n[character.audio]\nSpAwN=event:other\n", "duplicate audio key");
    reject("Spawn=../escape.wav\n", "mod-relative");
    reject("Spawn=C:/escape.wav\n", "mod-relative");
    reject("Spawn=https://example.org/file.ogg\n", "mod-relative");
    reject("Spawn=folder\\sound.wav\n", "mod-relative");
    reject("Spawn=folder//sound.wav\n", "mod-relative");
    reject("Spawn=folder/./sound.wav\n", "mod-relative");
    reject("Spawn=CON.wav\n", "mod-relative");
    reject("Spawn=empty.fsb\n", "mod-relative");
    reject("Spawn=event:\n", "native audio event");
    reject("Spawn=ok.wav,\n", "empty audio list");
    reject("Spawn=ok.wav,OK.WAV\n", "duplicate audio list");
    reject("Spawn=bad.wav\n", "RIFF/WAVE");
    reject("Spawn=bad.ogg\n", "Ogg Vorbis");
    reject("Spawn=missing.wav\n", "missing");
    reject("Volume=nan\nSpawn=ok.wav\n", "must be in");
    reject("Volume=1.01\nSpawn=ok.wav\n", "must be in");
    reject("Chance=-0.1\nSpawn=ok.wav\n", "must be in");
    reject("Cooldown=3601\nSpawn=ok.wav\n", "must be in");
    reject("IdleInterval=0\nIdle=ok.wav\n", "must be in");
    reject("Spawn.Volume=0.5\n", "no matching cue");
    reject("Spawn.MinDistance=5\n", "no matching cue");
    reject("Spatial=yes\nSpawn=ok.wav\n", "0 or 1");
    reject("MinDistance=0\nSpawn=ok.wav\n", "must be in");
    reject("Spawn=ok.wav\nSpawn.MinDistance=50\nSpawn.MaxDistance=40\n", "MaxDistance");
    reject("Spawn=ok.wav\nBanks=one.fev,two.fsb\n", "native .fev");
    reject("Spawn=ok.wav\nBanks=one,one.fev\n", "duplicate native");
    reject("Spawn=ok.wav\n[Character.Voice]\nIdle=bad.ogg\n", "Ogg Vorbis"); // Owner atomic.
    reject("Spawn=ok.wav\nReplace.=event:native\n", "invalid audio trigger");
    reject(std::string("Spawn=ok.wav\n\0Unexpected=bad.wav", 32), "NUL");
    std::string many;
    for (unsigned i = 0; i < 65; ++i) many += "Event" + std::to_string(i) + "=event:effect\n";
    reject(many, "at most 64 cues");
    reject("Spawn=event:a,event:b,event:c,event:d,event:e,event:f,event:g,event:h,event:i\n", "limit 8");
}
void isolation(const fs::path& root) {
    const auto bad = mod(root, "bad_owner", "[Character.Audio]\nSpawn=event:valid\n[Character.Voice]\nIdle=missing.ogg\n[Tool.Audio]\nMunitionFired=event:shot\n");
    const auto good = mod(root, "good_owner", "[Character.Audio]\nSpawn=event:spawn\n");
    std::vector<std::string> messages;
    const auto profiles = loadCustomAudio({bad, good}, {character(bad, "Bad"), character(good, "Good")}, {tool(bad)}, messages);
    CHECK(profiles.size() == 2 && logged(messages, "Custom audio Bad: REJECTED"));
    if (profiles.size() == 2) CHECK(profiles[0].tool && profiles[1].identity == "Good");
}
void limits(const fs::path& root) {
    const auto m = mod(root, "size", "[Character.Audio]\nSpawn=large.wav\n");
    large(fs::path(m.path) / "large.wav", (8u << 20) + 1);
    std::vector<std::string> messages;
    CHECK(loadCustomAudio({m}, {character(m)}, {}, messages).empty() && logged(messages, "size limit"));
    std::string ini = "[Character.Audio]\nFirst=";
    for (unsigned i = 0; i < 9; ++i) {
        large(fs::path(m.path) / ("big" + std::to_string(i) + ".wav"), 8u << 20);
        if (i == 8) ini += "\nSecond=";
        else if (i) ini += ',';
        ini += "big" + std::to_string(i) + ".wav";
    }
    ini += '\n'; write(fs::path(m.path) / "mod.ini", ini); messages.clear();
    CHECK(loadCustomAudio({m}, {character(m)}, {}, messages).empty() && logged(messages, "64 MiB"));
}
void links(const fs::path& root) {
    const auto m = mod(root, "linked", "[Character.Audio]\nSpawn=linked.wav\n");
    write(root / "outside.wav", "RIFFxxxxWAVE");
    const auto link = fs::path(m.path) / "linked.wav";
    if (!CreateSymbolicLinkW(link.c_str(), (root / "outside.wav").c_str(), 2)) {
        std::printf("SKIP optional symlink fixture (%lu).\n", GetLastError()); return;
    }
    std::vector<std::string> messages;
    CHECK(loadCustomAudio({m}, {character(m)}, {}, messages).empty() && logged(messages, "link"));
    fs::remove(link);
    write(root / "outside_dir/clip.wav", "RIFFxxxxWAVE");
    CHECK(CreateSymbolicLinkW((fs::path(m.path) / "folder").c_str(), (root / "outside_dir").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2));
    write(fs::path(m.path) / "mod.ini", "[Character.Audio]\nSpawn=folder/clip.wav\n"); messages.clear();
    CHECK(loadCustomAudio({m}, {character(m)}, {}, messages).empty() && logged(messages, "link"));
}
}
int main() {
    try { Fixture fixture; declarations(fixture.root); ignored(fixture.root); invalid(fixture.root); isolation(fixture.root); limits(fixture.root); links(fixture.root); }
    catch (const std::exception& error) { std::printf("EXCEPTION: %s\n", error.what()); ++failures; }
    std::printf("Custom audio manifest checks: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
