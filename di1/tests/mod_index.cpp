// SPDX-License-Identifier: GPL-3.0-only
// Mod discovery, validation and chunk matching, without a Lua VM.
#include "mods/mod_index.hpp"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

using namespace crabe::mods;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
}

static bool logged(const std::vector<std::string>& messages, std::string_view text) {
    for (const auto& message : messages) if (message.find(text) != std::string::npos) return true;
    return false;
}

int main() {
    char buffer[maxChunkName];
    CHECK(normalize("@Presentation\\Adventures.LUA", buffer) == 27 &&
        std::string_view(buffer, 27) == "presentation/adventures.lua");
    CHECK(normalize("=./a.lua", buffer) == 5 && std::string_view(buffer, 5) == "a.lua");
    CHECK(normalize("@", buffer) == 0);
    CHECK(normalize(std::string(maxChunkName, 'a'), buffer) == 0);

    // The game's precompiled chunks carry their source name in the header.
    const char header[] = "\x1bLua\x51\x00\x01\x04\x04\x04\x08\x00\x1d\x00\x00\x00@Presentation/Adventures.lua\x00\x00\x00";
    CHECK(chunkIdentity(header, sizeof(header) - 1, "=bytes") == "@Presentation/Adventures.lua");
    CHECK(chunkIdentity(header, 20, "=bytes") == "=bytes");  // Truncated header falls back.
    CHECK(chunkIdentity("return 1", 8, "@x.lua") == "@x.lua");
    CHECK(chunkIdentity("return 1", 8, nullptr).empty());
    CHECK(luaQuote("a\"\\\n\x7f") == "\"a\\\"\\\\\\010\\127\"");

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const fs::path root = fs::path(temp) / (L"literalcrab-mods-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    write(root / "alpha/mod.ini",
        "[Mod]\nName=Alpha Mod\n[Options]\nFlag=1\nText=hello world\nNegative=-3\n"
        "[Override]\nPresentation\\Adventures.lua=scripts/adventures.lua\n"
        "[Patch]\npresentation/pausemenu.lua=patches/pause.lua\npresentation/pausemenu.lua=patches/pause2.lua\n");
    write(root / "alpha/user.ini", "[Options]\nFlag=0\n");
    write(root / "alpha/scripts/adventures.lua", "return 'override'");
    write(root / "alpha/patches/pause.lua", "local result = ...");
    write(root / "alpha/patches/pause2.lua", "local result = ...");
    write(root / "beta/mod.ini", "[Override]\nadventures.lua=a.lua\npresentation/adventures.lua=a.lua\n[Patch]\nadventures.lua=p.lua\n");
    write(root / "beta/a.lua", "return 'beta'");
    write(root / "beta/p.lua", "return");
    write(root / "disabled/mod.ini", "[Patch]\nx.lua=p.lua\n");
    write(root / "disabled/user.ini", "[Mod]\nEnabled=0\n");
    write(root / "disabled/p.lua", "return");
    write(root / "escape/mod.ini", "[Patch]\nescape.lua=../alpha/patches/pause.lua\n");
    write(root / "bytecode/mod.ini", "[Patch]\nbytecode.lua=p.lua\n");
    write(root / "bytecode/p.lua", std::string_view("\x1bLua\x51", 5));
    write(root / "missing/mod.ini", "[Patch]\nfine.lua=p.lua\nmissing.lua=absent.lua\n");
    write(root / "missing/p.lua", "return");
    write(root / "empty/mod.ini", "[Mod]\nName=Empty\n");
    write(root / "gamma/mod.ini", "[Patch]\ncontains:TB_WorldPU_Fortress=p.lua\nCONTAINS:Select ITEM_X=p.lua\n");
    write(root / "gamma/p.lua", "return");
    write(root / "short/mod.ini", "[Patch]\ncontains:abc=p.lua\n");
    write(root / "short/p.lua", "return");
    write(root / "hub/mod.ini", "[Mod]\nName=Hub\n[Engine]\nTriggerGuiScreen=1\nUnused=0\n");
    write(root / "badengine/mod.ini", "[Engine]\nTriggerGuiScreen=yes\n");
    write(root / "notamod/readme.txt", "no manifest");

    std::vector<std::string> messages;
    const auto index = Index::load(root, messages);
    for (const auto& message : messages) std::printf("  %s\n", message.c_str());
    CHECK(index.mods.size() == 5 && index.mods[0].name == "Alpha Mod" && index.mods[1].name == "beta" && index.mods[2].scripts == 0);
    // Engine features: only enabled names are requested; the host decides what they mean.
    CHECK(index.mods.size() == 5 && index.mods[4].name == "Hub" && index.mods[4].engine == std::vector<std::string>{"TriggerGuiScreen"});
    CHECK(index.mods.size() == 5 && index.mods[0].engine.empty());
    CHECK(logged(messages, "Mod badengine skipped: [Engine] values must be 0 or 1: TriggerGuiScreen"));
    CHECK(logged(messages, "Mod short skipped: content marker shorter than 8 bytes"));
    CHECK(index.contents.size() == 2);
    CHECK(logged(messages, "Mod disabled: disabled."));
    CHECK(logged(messages, "Mod escape skipped"));
    CHECK(logged(messages, "Mod bytecode skipped"));
    CHECK(logged(messages, "Mod missing skipped"));  // One bad file rejects the whole mod.
    CHECK(logged(messages, "Mod empty (Empty): 0 script(s)."));  // May still bring assets.
    CHECK(logged(messages, "override of presentation/adventures.lua ignored; alpha already replaces it"));
    CHECK(index.targets.size() == 3);

    const Target* matches[maxMatches];
    // Precompiled game chunk: the longer configured path is the more specific match.
    auto count = index.find(chunkIdentity(header, sizeof(header) - 1, "=bytes"), "", matches);
    CHECK(count == 2 && matches[0]->path == "presentation/adventures.lua" && matches[1]->path == "adventures.lua");
    CHECK(count == 2 && index.scripts[matches[0]->override].source == "return 'override'");
    CHECK(count == 2 && matches[0]->patches.empty() && matches[1]->patches.size() == 1);
    CHECK(count == 2 && index.scripts[matches[1]->override].chunk == "@mods/beta/a.lua");
    count = index.find("@ASSETS\\Presentation\\PauseMenu.lua", "", matches);
    CHECK(count == 1 && matches[0]->patches.size() == 2);
    if (count == 1) {
        const auto& patch = index.scripts[matches[0]->patches[0]];
        CHECK(patch.chunk == "@mods/alpha/patches/pause.lua" && patch.body == "local result = ...");
        // user.ini overrides the shipped default; integers stay numbers, text is quoted.
        CHECK(patch.source.find("[\"Flag\"]=0,[\"Text\"]=\"hello world\",[\"Negative\"]=-3,") != std::string::npos);
        CHECK(patch.source.find("local label = \"alpha/patches/pause.lua\"") != std::string::npos);
    }
    CHECK(index.find("@notpresentation/adventures.lua", "", matches) == 1);  // Suffix must start at '/'.
    CHECK(index.find("@presentation/other.lua", "", matches) == 0);
    CHECK(index.find("", "", matches) == 0);
    // Content markers: exact, case-sensitive, and only for chunks without a name.
    const std::string_view data = "local t = { 'TB_WorldPU_Fortress', 'Select ITEM_X' }";
    count = index.find("", data, matches);
    CHECK(count == 2 && matches[0]->content && matches[0]->path == "TB_WorldPU_Fortress" && matches[1]->path == "Select ITEM_X");
    CHECK(index.find("=", data, matches) == 2);
    CHECK(index.find("@presentation/named.lua", data, matches) == 0);
    CHECK(index.find("", "tb_worldpu_fortress", matches) == 0);
    CHECK(Index::load(root / "absent", messages).empty());

    // Host patches: listed as LiteralCrab, after the mods' patches, found by the usual matching.
    auto hosted = Index::load(root, messages);
    const auto before = hosted.scripts.size();
    hosted.addBuiltinPatch("ToyBoxStats_Limits.lua", "toybox_limits", "local x = 1", "{Scale=10}");
    hosted.addBuiltinPatch("Presentation/PauseMenu.lua", "pause", "return", "{}");
    CHECK(hosted.scripts.size() == before + 2 && hosted.mods.back().folder == "LiteralCrab");
    count = hosted.find("@GameDB/Core/ToyBoxStats_Limits.lua", "", matches);
    CHECK(count == 1 && matches[0]->patches.size() == 1);
    if (count == 1) {
        const auto& patch = hosted.scripts[matches[0]->patches[0]];
        CHECK(patch.chunk == "@LiteralCrab/toybox_limits" && patch.body == "local x = 1");
        CHECK(patch.source.find("local options = {Scale=10}") != std::string::npos);
    }
    count = hosted.find("@presentation/pausemenu.lua", "", matches);
    CHECK(count == 1 && matches[0]->patches.size() == 3 &&
        hosted.scripts[matches[0]->patches[2]].chunk == "@LiteralCrab/pause");

    fs::remove_all(root);
    if (failures) return 1;
    std::puts("PASS: mod discovery, validation and chunk matching");
    return 0;
}
