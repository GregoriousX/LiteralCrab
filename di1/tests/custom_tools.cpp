// SPDX-License-Identifier: GPL-3.0-only
// Standalone tools use synthetic native-format fixtures. No shipped game data
// is included, and all files created by this executable belong to its temp root.
#include "di1/custom_tools.hpp"
#include "di1/character_gameplay.hpp"
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace crabe::di1;
namespace fs = std::filesystem;
namespace {
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
struct Fixture {
    fs::path root;
    Fixture() {
        root = fs::absolute(fs::temp_directory_path()) / ("literalcrab-custom-tools-" +
            std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        if (!fs::create_directory(root)) throw std::runtime_error("test fixture root already exists");
    }
    ~Fixture() {
        if (root.parent_path() == fs::absolute(fs::temp_directory_path()) &&
            root.filename().wstring().starts_with(L"literalcrab-custom-tools-")) {
            std::error_code error; fs::remove_all(root, error);
        }
    }
};
void write(const fs::path& path, std::string_view bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!stream) throw std::runtime_error("cannot write test fixture");
}
std::string read(const crabe::mods::AssetIndex& assets, std::string_view name) {
    const auto* member = assets.member(name); std::string bytes;
    CHECK(member && assets.read(*member, bytes)); return bytes;
}
void generated(crabe::mods::AssetIndex& assets, std::string name, std::string bytes) {
    crabe::mods::AssetMember member; member.name = std::move(name);
    member.generated = std::make_shared<const std::string>(std::move(bytes));
    assets.members.push_back(std::move(member));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}
bool logged(const std::vector<std::string>& messages, std::string_view text) {
    return std::any_of(messages.begin(), messages.end(), [&](const auto& message) { return message.find(text) != message.npos; });
}
std::string manifest(std::string_view id = "LCR_TS3_ZURG_BLASTER") {
    return "[Mod]\nName=Standalone tool test\n[Tool]\nId=" + std::string(id) +
        "\nBaseTool=ITEM_BLASTER\nCharacters=LiteralCrab_Zurg\nSourceActor=tool_zurgbossblaster\n"
        "SourceDNA=characters/ZurgBossBlaster.dnax\nSourceScript=characters/ZurgBossBlaster.sx\n"
        "WeaponSource=ZurgBossBlasterShot\nDisplayName=TS3 Zurg Blaster\nDescription=Custom blaster description\n"
        "Model=characters/customgun/customgun\nAssetDirectory=tool-assets\nProjectileRadius=0.15\n"
        "ProjectileScale=0.1\nProjectileSpeed=35\nProjectileLifetime=3\nProjectileDamage=17\n"
        "PreloadActors=tool_flash\nPreloadFiles=Cho|choreographies/custom_flash/custom_flash\nPreloadIncludes=biped_avatar\n";
}
crabe::mods::Mod mod(const fs::path& root, std::string folder, std::string_view content) {
    crabe::mods::Mod result;
    result.folder = result.name = std::move(folder); result.path = (root / result.folder).wstring();
    write(fs::path(result.path) / "mod.ini", content); return result;
}
std::string replace(std::string source, std::string_view before, std::string_view after) {
    const auto at = source.find(before); if (at == source.npos) throw std::runtime_error("test replacement missing");
    source.replace(at, before.size(), after); return source;
}
void manifests(const fs::path& root) {
    unsigned serial{};
    const auto make = [&](std::string_view source) { return mod(root, "tool" + std::to_string(++serial), source); };
    std::vector<std::string> messages;
    const auto valid = make(manifest());
    auto tools = loadCustomTools({valid}, messages);
    CHECK(tools.size() == 1);
    if (!tools.empty()) {
        const auto& t = tools[0];
        CHECK(t.table.id == "LCR_TS3_ZURG_BLASTER" && t.table.baseTool == "ITEM_BLASTER");
        CHECK(t.table.actor.starts_with("lcr_") && t.table.actor != t.sourceActor && t.table.inventoryProfiles.empty());
        CHECK(t.characters == std::vector<std::string>{"LiteralCrab_Zurg"});
        CHECK(t.sourceActor == "tool_zurgbossblaster" && t.sourceDna == "characters/ZurgBossBlaster.dnax");
        CHECK(t.radius == 0.15 && t.scale == 0.1 && t.speed == 35 && t.lifetime == 3 && t.damage == 17);
        CHECK(t.preloadActors.size() == 1 && t.preloadFiles.size() == 1 && t.preloadIncludes.size() == 1);
        CHECK(t.modPath == valid.path && t.assetDirectory == "tool-assets");
        setCustomToolLabels(tools);
        const auto* name = customToolText("LCR_TS3_ZURG_BLASTER_name");
        const auto* description = customToolText("LCR_TS3_ZURG_BLASTER_description");
        CHECK(name && std::string(name) == "TS3 Zurg Blaster");
        CHECK(description && std::string(description) == "Custom blaster description");
        CHECK(!customToolText("ITEM_BLASTER_name") && !customToolText("not_registered") && !customToolText(nullptr));
        setCustomToolLabels({}); CHECK(!customToolText("LCR_TS3_ZURG_BLASTER_name"));
    }
    auto controlled = manifest() + "ControlSource=ITEM_EXISTING\n";
    tools = loadCustomTools({make(controlled)}, messages);
    CHECK(tools.size() == 1 && tools[0].table.controlSource == "ITEM_EXISTING");
    tools=loadCustomTools({make(manifest()+"ModelHandle=ControlGunHandle\n")},messages);
    CHECK(tools.size()==1&&tools[0].table.modelHandle=="ControlGunHandle");
    CHECK(loadCustomTools({make(manifest()+"ModelHandle=bad/path\n")},messages).empty());
    const std::string minimal = "[Tool]\nId=LCR_SIMPLE_TOOL\nBaseTool=ITEM_BLASTER\nCharacters=*\n"
        "SourceActor=tool_blaster\nSourceDNA=characters/Blaster.dnax\n";
    tools = loadCustomTools({make(minimal)}, messages);
    CHECK(tools.size() == 1 && tools[0].sourceScript.empty() && tools[0].weaponSource.empty() && tools[0].displayName == "LCR_SIMPLE_TOOL");
    // The script and gun are inferred from the native tool at startup now.
    CHECK(loadCustomTools({make(minimal + "WeaponSource=NativeShot\n")}, messages).size() == 1);
    CHECK(loadCustomTools({make(minimal + "ProjectileScale=0.5\n")}, messages).size() == 1);
    CHECK(loadCustomTools({make(minimal + "WeaponSource=bad/name\n")}, messages).empty());
    // Source alone names the tool to copy; SourceActor and SourceDNA come as a pair.
    const std::string sourced = "[Tool]\nId=LCR_SOURCED_TOOL\nSource=ITEM_ZURG_BOSS_BLASTER\nCharacters=*\n";
    tools = loadCustomTools({make(sourced)}, messages);
    CHECK(tools.size() == 1 && tools[0].source == "ITEM_ZURG_BOSS_BLASTER" && tools[0].table.baseTool.empty());
    CHECK(loadCustomTools({make("[Tool]\nId=LCR_NO_SOURCE\nCharacters=*\n")}, messages).empty());
    CHECK(loadCustomTools({make(sourced + "SourceActor=tool_blaster\n")}, messages).empty());
    for (const auto& change : std::vector<std::pair<std::string, std::string>>{
        {"Id=LCR_TS3_ZURG_BLASTER", "Id=ITEM_BLASTER"}, {"Id=LCR_TS3_ZURG_BLASTER", "Id=LCR_"},
        {"Id=LCR_TS3_ZURG_BLASTER", "Id=LCR_BAD/PATH"}, {"Characters=LiteralCrab_Zurg", "Characters="},
        {"Characters=LiteralCrab_Zurg", "Characters=LiteralCrab_Zurg,literalcrab_zurg"},
        {"Characters=LiteralCrab_Zurg", "Characters=*,LiteralCrab_Zurg"},
        {"SourceActor=tool_zurgbossblaster", "SourceActor=bad/path"},
        {"SourceDNA=characters/ZurgBossBlaster.dnax", "SourceDNA=../native.dnax"},
        {"SourceScript=characters/ZurgBossBlaster.sx", "SourceScript=characters/invalid.lua"},
        {"AssetDirectory=tool-assets", "AssetDirectory=../outside"},
        {"AssetDirectory=tool-assets", "AssetDirectory=assets"},
        {"ProjectileRadius=0.15", "ProjectileRadius=-1"}, {"ProjectileScale=0.1", "ProjectileScale=nan"},
        {"ProjectileSpeed=35", "ProjectileSpeed=inf"}, {"ProjectileLifetime=3", "ProjectileLifetime=-1"},
        {"ProjectileDamage=17", "ProjectileDamage=-1"},
        {"PreloadFiles=Cho|choreographies/custom_flash/custom_flash", "PreloadFiles=Unknown|characters/model"},
        {"PreloadIncludes=biped_avatar", "PreloadIncludes=../escape"}}) {
        messages.clear();
        const auto rejected = loadCustomTools({make(replace(manifest(), change.first, change.second))}, messages);
        CHECK(rejected.empty() && logged(messages, "REJECTED"));
    }
    for (const auto* extra : {"UnknownToolOption=1\n", "Id=LCR_DUPLICATE\n", "[Tool]\nId=LCR_SECOND\n"}) {
        messages.clear(); CHECK(loadCustomTools({make(manifest() + extra)}, messages).empty());
        CHECK(logged(messages, "REJECTED"));
    }
    messages.clear(); tools = loadCustomTools({valid, make(manifest())}, messages);
    CHECK(tools.size() == 1 && logged(messages, "REJECTED"));
    CHECK(loadCustomTools({valid, make(manifest("lcr_ts3_zurg_blaster"))}, messages).size() == 1);
    const auto invalid = make(replace(manifest(), "BaseTool=ITEM_BLASTER", "BaseTool=bad/path"));
    CHECK(loadCustomTools({invalid, valid}, messages).size() == 1);
    CHECK(loadCustomTools({make("[Mod]\nName=No tool\n")}, messages).empty());
    std::vector<crabe::mods::Mod> many;
    for (unsigned i = 0; i < 65; ++i) many.push_back(make(manifest("LCR_TEST_" + std::to_string(i))));
    CHECK(loadCustomTools(many, messages).size() == 64);

    // The existing enabled mod index, including user.ini, owns enablement;
    // user.ini cannot replace the standalone tool's identity or source paths.
    const auto enabled = root / "enabled-index";
    (void)mod(enabled, "custom-tool", manifest());
    write(enabled / "custom-tool/user.ini", "[Tool]\nId=LCR_USER_REPLACEMENT\n");
    auto index = crabe::mods::Index::load(enabled.wstring(), messages);
    tools = loadCustomTools(index.mods, messages);
    CHECK(tools.size() == 1 && tools[0].table.id == "LCR_TS3_ZURG_BLASTER");
    write(enabled / "custom-tool/user.ini", "[Mod]\nEnabled=0\n");
    index = crabe::mods::Index::load(enabled.wstring(), messages);
    CHECK(loadCustomTools(index.mods, messages).empty());
}

void word(std::string& bytes, std::size_t at, unsigned value) { std::memcpy(bytes.data() + at, &value, 4); }
unsigned word(std::string_view bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("fixture word outside bounds");
    unsigned value{}; std::memcpy(&value, bytes.data() + at, 4); return value;
}
void append(std::string& bytes, unsigned value) { const auto at = bytes.size(); bytes.resize(at + 4); word(bytes, at, value); }
std::array<std::string, 13> blocks(std::string_view data) {
    std::array<std::string, 13> result; std::size_t at{};
    for (auto& block : result) { const auto size = word(data, at); block = data.substr(at, size); at += size; }
    CHECK(at == data.size()); return result;
}
std::string toolTable() {
    std::array<std::string, 13> data; for (auto& block : data) block.resize(4, '\0');
    const auto text = [&](std::string_view value) { const auto at = static_cast<unsigned>(data[0].size() - 4); data[0] += value; data[0] += '\0'; return at; };
    // One native blaster actor and four ordered item/prop/ability rows.
    data[10].resize(72, '\0'); word(data[10], 4, 1);
    word(data[10], 8, text("BlasterDescriptor")); word(data[10], 12, nativeToolHash("BlasterDescriptor"));
    word(data[10], 16, text("tool_zurgbossblaster"));
    append(data[1], 0); append(data[1], ~0u);
    data[11].resize(8 + 4 * 76, '\0'); word(data[11], 4, 4);
    const char* names[] = {"ITEM_NONE", "ITEM_BLASTER", "PROP_NONE", "ABILITY_NONE"};
    for (unsigned i = 0; i < 4; ++i) {
        const auto at = 8 + i * 76;
        word(data[11], at, text(names[i])); word(data[11], at + 4, nativeToolHash(names[i]));
        word(data[11], at + 8, text(i == 2 ? "Props" : i == 3 ? "Abilities" : "Items"));
        word(data[11], at + 12, text("UP_Blaster")); word(data[11], at + 20, text(names[i]));
        word(data[11], at + 24, 0);
    }
    // Both profiles share the same original stream; extension must leave the
    // profile bytes and stream untouched for all native avatars and NPCs.
    for (unsigned i : {0u, 1u, 2u, 3u, ~0u}) append(data[1], i);
    data[12].resize(24, '\0'); word(data[12], 4, 2);
    word(data[12], 8, text("AV_Zurg")); word(data[12], 12, 2);
    word(data[12], 16, text("AV_Buzz")); word(data[12], 20, 2);
    std::string bytes;
    for (auto& block : data) { word(block, 0, static_cast<unsigned>(block.size())); bytes += block; }
    return bytes;
}
std::string script() {
    std::string bytes(0x74, '\0'); std::memcpy(bytes.data(), "SLNG\x0c\x10\0\0\x78\x56\x34\x12", 12);
    word(bytes, 0x30, 1); word(bytes, 0x64, 0x70 - 0x65); word(bytes, 0x70, static_cast<unsigned>(bytes.size() - 0x71));
    bytes += "ZurgBossBlasterShot"; bytes += '\0'; word(bytes, 12, static_cast<unsigned>(bytes.size())); return bytes;
}
std::string literal(std::string_view bytes) {
    const auto table = 0x65u + word(bytes, 0x64), start = table + 1u + word(bytes, table);
    const auto end = bytes.find('\0', start); if (end == bytes.npos) throw std::runtime_error("unterminated fixture literal");
    return std::string(bytes.substr(start, end - start));
}
const std::string dna = "DNA={AgentGadgets={Identification={Name='Zurg Blaster',ActorTypeMask='Tool'},"
    "Script={ScriptName='characters/ZurgBossBlaster.sx',Other=17},"
    "EntityInstance={ModelName='characters/zurgbossblaster/zurgbossblaster'}},Components={Kept={Value=19}}};return 23";
CustomFigure figure() {
    CustomFigure f;
    f.sku = 1000900; f.name = "LiteralCrab_Zurg"; f.actor = f.gateway = "lcr_zurg";
    f.baseCharacter = "AV_Buzz"; f.displayName = "Zurg"; f.playset = "RumpusRoom";
    f.gameplay.sourceActor = "av_zurg"; f.gameplay.sourceDna = "characters/ts_zurg.dnax"; return f;
}
std::vector<CustomTool> prepared(const fs::path& root, crabe::mods::AssetIndex& assets, std::vector<CustomFigure>& figures) {
    const auto first = mod(root, "first", manifest());
    const auto second = mod(root, "second", manifest("LCR_SECOND_BLASTER"));
    for (const auto& m : {first, second}) {
        write(fs::path(m.path) / "tool-assets/characters/ZurgBossBlaster.dnax", dna);
        write(fs::path(m.path) / "tool-assets/characters/ZurgBossBlaster.sx", script());
        write(fs::path(m.path) / "tool-assets/characters/customgun/customgun.bent", m.folder + " private model");
    }
    generated(assets, "var/toolset/toolset.bin", toolTable());
    generated(assets, "characters/zurgbossblaster.dnax", dna);
    generated(assets, "characters/zurgbossblaster.sx", script());
    generated(assets, "characters/customgun/customgun.bent", "original model must not change");
    std::vector<std::string> messages;
    auto tools = loadCustomTools({first, second}, messages);
    prepareCustomTools(assets, root.wstring(), tools, figures, messages);
    if (tools.size() != 2) for (const auto& message : messages) std::printf("prepare: %s\n", message.c_str());
    return tools;
}
bool run(lua_State* state, std::string_view source) {
    if (luaL_loadbuffer(state, source.data(), source.size(), "=custom-tool-test") || lua_pcall(state, 0, 0, 0)) {
        std::printf("Lua: %s\n", lua_tostring(state, -1)); lua_settop(state, 0); return false;
    }
    return true;
}
bool patch(lua_State* state, const std::string& body) {
    return run(state, "(function(...)\n" + body + "\nend)(nil,{},function()end,_G)");
}
std::string q(std::string_view value) { return crabe::mods::luaQuote(value); }

void preparation(const fs::path& root) {
    crabe::mods::AssetIndex assets; std::vector<CustomFigure> figures{figure()};
    const auto original = toolTable();
    const auto originalBlocks = blocks(original);
    auto tools = prepared(root, assets, figures);
    CHECK(tools.size() == 2); if (tools.size() != 2) return;
    const auto& a = tools[0]; const auto& b = tools[1];
    CHECK(a.table.actor != b.table.actor && a.privateDna != b.privateDna && a.privateScript != b.privateScript && a.privateWeapon != b.privateWeapon);
    CHECK(a.targetSkus == std::vector<std::uint32_t>{figure().sku} && b.targetSkus == a.targetSkus);
    CHECK(a.table.inventoryProfiles.empty() && b.table.inventoryProfiles.empty());
    CHECK(std::find(figures[0].gameplay.extraTools.begin(), figures[0].gameplay.extraTools.end(), a.table.id) != figures[0].gameplay.extraTools.end());
    CHECK(std::find(figures[0].gameplay.extraTools.begin(), figures[0].gameplay.extraTools.end(), b.table.id) != figures[0].gameplay.extraTools.end());
    CHECK(std::find(figures[0].gameplay.preloadActors.begin(), figures[0].gameplay.preloadActors.end(), a.table.actor) != figures[0].gameplay.preloadActors.end());
    CHECK(read(assets, "characters/zurgbossblaster.dnax") == dna);
    CHECK(read(assets, "characters/zurgbossblaster.sx") == script());
    CHECK(read(assets, "characters/customgun/customgun.bent") == "original model must not change");
    const auto after = blocks(read(assets, "var/toolset/toolset.bin"));
    CHECK(word(after[11], 4) == 6 && word(after[10], 4) == 3);
    CHECK(after[12] == originalBlocks[12]);
    CHECK(after[1].substr(4, originalBlocks[1].size() - 4) == originalBlocks[1].substr(4));
    CHECK(after[11].substr(8, originalBlocks[11].size() - 8) == originalBlocks[11].substr(8));
    for (const auto& t : tools) {
        CHECK(t.hash == nativeToolHash(t.table.id));
        CHECK(read(assets, t.privateDna).find("LiteralCrabTool:" + t.table.id) != std::string::npos);
        const auto privateScript = read(assets, t.privateScript);
        CHECK(literal(privateScript) == t.privateWeapon);
        CHECK(privateScript.substr(16, 0x60) == script().substr(16, 0x60));
        CHECK(privateAssetPath(t.assetAliases, "characters/customgun/customgun.bent") != "characters/customgun/customgun.bent");
        const auto model = read(assets, privateAssetPath(t.assetAliases, "characters/customgun/customgun.bent"));
        CHECK(model == (&t == &a ? "first private model" : "second private model"));
    }
    // A source-only custom avatar needs its own profile once standalone tools
    // are added, even without InventorySource. Keep NONE/default group order.
    figures[0].gameplay.item=a.table.id;
    resolveCustomInventory(read(assets,"var/toolset/toolset.bin"),figures[0]);
    CHECK(figures[0].gameplay.inventoryRows==std::vector<std::uint32_t>({0,1,4,5,2,3,~0u}));
    CHECK(figures[0].gameplay.inventoryHashes.size()==6);

    // Equipping belongs to the enabled standalone mod. Two tools cannot both
    // overwrite one starting slot, and the legacy private gun is not reused.
    crabe::mods::AssetIndex equippedAssets;generated(equippedAssets,"var/toolset/toolset.bin",original);
    auto equippedTools=tools;for(auto& t:equippedTools) t.equip=true;
    auto legacy=figure();legacy.gameplay.item="ITEM_BLASTER";legacy.gameplay.itemWeapon.source="OldGun";
    legacy.gameplay.controlSources[0]="ITEM_BLASTER";legacy.gameplay.toolDisplayNames[0]="OldGunLabel";
    std::vector<CustomFigure> equippedFigures{legacy};std::vector<std::string> equippedMessages;
    prepareCustomTools(equippedAssets,root.wstring(),equippedTools,equippedFigures,equippedMessages);
    CHECK(equippedTools.size()==1&&equippedFigures.size()==1);
    CHECK(equippedFigures[0].gameplay.item==a.table.id&&!equippedFigures[0].gameplay.itemWeapon.enabled());
    CHECK(equippedFigures[0].gameplay.controlSources[0].empty()&&equippedFigures[0].gameplay.toolDisplayNames[0].empty());
    CHECK(equippedFigures[0].gameplay.extraTools==std::vector<std::string>{a.table.id});
    CHECK(logged(equippedMessages,"already equips"));
    CHECK(legacy.gameplay.item=="ITEM_BLASTER"&&legacy.gameplay.itemWeapon.source=="OldGun");
    // Invalid target selection publishes no new actor/resources/tool rows and
    // does not change the original inventory or an unrelated custom figure.
    crabe::mods::AssetIndex rejected; generated(rejected, "var/toolset/toolset.bin", original);
    auto bad = tools[0]; bad.table.id = "LCR_MISSING_TARGET"; bad.characters = {"NoSuchCustomFigure"};
    bad.assetAliases.clear(); bad.targetSkus.clear();
    std::vector<CustomTool> badTools{bad}; std::vector<CustomFigure> unchanged{figure()}; std::vector<std::string> messages;
    prepareCustomTools(rejected, root.wstring(), badTools, unchanged, messages);
    CHECK(badTools.empty() && read(rejected, "var/toolset/toolset.bin") == original);
    CHECK(unchanged[0].gameplay.extraTools.empty() && unchanged[0].gameplay.preloadActors.empty());

    bad=tools[0];bad.sourceActor="tool_missing_actor";badTools={bad};
    prepareCustomTools(rejected,root.wstring(),badTools,unchanged,messages);
    CHECK(badTools.empty()&&logged(messages,"not an existing tool actor"));
    CHECK(read(rejected,"var/toolset/toolset.bin")==original&&unchanged[0].gameplay.extraTools.empty());

    const auto lateMod = mod(root, "late-failure", manifest("LCR_LATE_FAILURE"));
    write(fs::path(lateMod.path) / "tool-assets/a/opaque.bent", "valid earlier private resource");
    write(fs::path(lateMod.path) / "tool-assets/characters/ZurgBossBlaster.dnax", dna);
    // Valid SLNG envelope without the declared weapon literal: generic asset
    // mounting succeeds, but weapon remapping must reject the whole tool.
    auto wrongScript = script();
    const auto sourceAt = wrongScript.find("ZurgBossBlasterShot");
    wrongScript.replace(sourceAt, std::strlen("ZurgBossBlasterShot"), "OtherWeaponLiteral");
    word(wrongScript, 12, static_cast<unsigned>(wrongScript.size()));
    write(fs::path(lateMod.path) / "tool-assets/characters/ZurgBossBlaster.sx", wrongScript);
    write(fs::path(lateMod.path) / "tool-assets/characters/customgun/customgun.bent", "model");
    badTools = loadCustomTools({lateMod}, messages);
    const auto beforeMembers = rejected.members;
    const auto beforeRedirects = rejected.redirects;
    prepareCustomTools(rejected, root.wstring(), badTools, unchanged, messages);
    CHECK(badTools.empty() && rejected.members.size() == beforeMembers.size() && rejected.redirects.size() == beforeRedirects.size());
    for (std::size_t i = 0; i < rejected.members.size() && i < beforeMembers.size(); ++i)
        CHECK(rejected.members[i].name == beforeMembers[i].name && rejected.members[i].generated == beforeMembers[i].generated);
    CHECK(unchanged[0].gameplay.extraTools.empty() && unchanged[0].gameplay.preloadActors.empty());
}

crabe::mods::LuaApi luaApi(lua_State* state) {
    crabe::mods::LuaApi api{};
    api.loadBuffer = &luaL_loadbuffer; api.getTop = &lua_gettop; api.setTop = &lua_settop;
    api.pushValue = &lua_pushvalue; api.insert = &lua_insert; api.pushClosure = &lua_pushcclosure;
    api.pcall = &lua_pcall; api.toLString = &lua_tolstring;
    for (const char* library : {"_G", "table", "string", "math"}) {
        lua_getglobal(state, library);
        const std::string prefix = std::strcmp(library, "_G") ? std::string(library) + "." : "";
        for (lua_pushnil(state); lua_next(state, -2); lua_pop(state, 1))
            if (lua_type(state, -2) == LUA_TSTRING && lua_iscfunction(state, -1))
                api.library.push_back({prefix + lua_tostring(state, -2), lua_tocfunction(state, -1)});
        lua_pop(state, 1);
    }
    return api;
}
void taggedDna(lua_State* state, const std::vector<CustomTool>& tools, const crabe::mods::AssetIndex& assets) {
    crabe::mods::Index index; addCustomToolPatches(index, tools);
    CHECK(crabe::mods::configure(luaApi(state), std::move(index), [](const char*, ...) {}, false));
    const auto execute = [&](const std::string& bytes, const char* name, const std::string& scriptName, const std::string& model) {
        const auto loaded = crabe::mods::loadBuffer(state, bytes.data(), bytes.size(), name);
        CHECK(loaded == 0);
        if (loaded) { std::printf("Lua load: %s\n", lua_tostring(state, -1)); lua_settop(state, 0); return; }
        // Match native data chunks: the source and patch run in an empty
        // environment; library functions are supplied by the mod wrapper.
        lua_newtable(state); lua_setfenv(state, -2); lua_pushvalue(state, -1);
        const auto called = lua_pcall(state, 0, 1, 0); CHECK(called == 0);
        if (called) { std::printf("Lua execute: %s\n", lua_tostring(state, -1)); lua_settop(state, 0); return; }
        CHECK(lua_tonumber(state, -1) == 23); lua_pop(state, 1);
        lua_getfenv(state, -1); lua_getfield(state, -1, "DNA"); lua_getfield(state, -1, "AgentGadgets");
        lua_getfield(state, -1, "Script"); lua_getfield(state, -1, "ScriptName");
        CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == scriptName); lua_pop(state, 2);
        lua_getfield(state, -1, "EntityInstance"); lua_getfield(state, -1, "ModelName");
        CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == model); lua_settop(state, 0);
    };
    CHECK(luaL_loadbuffer(state, dna.data(), dna.size(), "=native-tool-dna") == 0);
    std::string binary;
    CHECK(lua_dump(state, [](lua_State*, const void* data, size_t count, void* out) {
        static_cast<std::string*>(out)->append(static_cast<const char*>(data), count); return 0;
    }, &binary) == 0); lua_pop(state, 1);
    for (const auto& t : tools) {
        const auto model = privateAssetPath(t.assetAliases, t.model);
        execute(read(assets, t.privateDna), t.privateDna.c_str(), t.privateScript, model);
        execute(identifyCustomDna(binary, "LiteralCrabTool:" + t.table.id), "", t.privateScript, model);
    }
    execute(dna, "characters/zurgbossblaster.dnax", "characters/ZurgBossBlaster.sx", "characters/zurgbossblaster/zurgbossblaster");
    execute(binary, "", "characters/ZurgBossBlaster.sx", "characters/zurgbossblaster/zurgbossblaster");
    execute(identifyCustomDna(binary, "LiteralCrabTool:LCR_UNREGISTERED"), "", "characters/ZurgBossBlaster.sx", "characters/zurgbossblaster/zurgbossblaster");
}

void runtime(const fs::path& root) {
    crabe::mods::AssetIndex assets; std::vector<CustomFigure> figures{figure()};
    auto tools = prepared(root, assets, figures); CHECK(tools.size() == 2); if (tools.size() != 2) return;
    const auto& t = tools[0];
    auto* state = luaL_newstate(); luaL_openlibs(state);
    CHECK(run(state, "disabled={};LiteralCrab={DisableCustomFigure=function(sku)disabled[#disabled+1]=sku end}"));
    CHECK(run(state, "Data={Records={{Name='tool_zurgbossblaster',Type='Other',SubType='NativeTool',Parms='DNAFile=characters/ZurgBossBlaster.dnax;Other=1;'}}};originalActor=Data.Records[1]"));
    CHECK(patch(state, customToolActorPatch(tools))); CHECK(patch(state, customToolActorPatch(tools)));
    CHECK(run(state, "assert(#Data.Records==3 and Data.Records[1]==originalActor);assert(originalActor.Parms=='DNAFile=characters/ZurgBossBlaster.dnax;Other=1;');assert(Data.Records[2].Name==" + q(t.table.actor) + ");assert(Data.Records[2].Type=='Other' and Data.Records[2].SubType=='NativeTool');assert(Data.Records[2].Parms=='DNAFile='.." + q(t.privateDna) + "..';Other=1;');assert(#disabled==0)"));
    CHECK(run(state, "DataMap={Actor={tool_zurgbossblaster={Files={'DNA|characters/ZurgBossBlaster.dnax','Script|characters/ZurgBossBlaster.sx','Entity|characters/customgun/customgun'},Includes={'native|Include'},Kept=17}}};originalDependencies=DataMap.Actor.tool_zurgbossblaster"));
    CHECK(patch(state, customToolDependenciesPatch(tools))); CHECK(patch(state, customToolDependenciesPatch(tools)));
    CHECK(run(state, "local row=DataMap.Actor[" + q(t.table.actor) + "];assert(row and row~=originalDependencies and row.Kept==17);assert(row.Files~=originalDependencies.Files and row.Includes~=originalDependencies.Includes);assert(row.Files[1]=='DNA|'.." + q(t.privateDna) + " and row.Files[2]=='Script|'.." + q(t.privateScript) + ");assert(originalDependencies.Files[1]=='DNA|characters/ZurgBossBlaster.dnax' and #originalDependencies.Files==3 and #originalDependencies.Includes==1);assert(#disabled==0)"));
    CHECK(run(state, dna)); CHECK(patch(state, customToolDnaPatch(t))); CHECK(patch(state, customToolDnaPatch(t)));
    CHECK(run(state, "assert(DNA.AgentGadgets.Script.ScriptName==" + q(t.privateScript) + ");assert(DNA.AgentGadgets.Script.Other==17 and DNA.Components.Kept.Value==19);assert(DNA.AgentGadgets.EntityInstance.ModelName==" + q(privateAssetPath(t.assetAliases, t.model)) + ")"));
    CHECK(run(state, "weapon={{name='ZurgBossBlasterShot',class='gun',projectile='NativeShot',Kept=17}};proj={{name='NativeShot',class='widget',collisionRadius=5,scale=1,launchSpeed={0,0,9},lifetime=9,damage=99,Kept=19}};nativeWeapon=weapon[1];nativeShot=proj[1]"));
    CHECK(patch(state, customToolWeaponPatch(tools))); CHECK(patch(state, customToolWeaponPatch(tools)));
    CHECK(run(state, "assert(#weapon==3 and #proj==3 and weapon[1]==nativeWeapon and proj[1]==nativeShot);assert(nativeWeapon.projectile=='NativeShot' and nativeShot.collisionRadius==5 and nativeShot.launchSpeed[3]==9);local gun,shot=weapon[2],proj[2];assert(gun.name==" + q(t.privateWeapon) + " and gun.projectile==gun.name and shot.name==gun.name);assert(gun.Kept==17 and shot.Kept==19);assert(shot.collisionRadius==0.15 and shot.scale==0.1 and shot.launchSpeed[3]==35 and shot.lifetime==3 and shot.damage==17);assert(shot.launchSpeed~=nativeShot.launchSpeed and #disabled==0)"));

    // A pre-existing unowned row must stay intact and disable only the figure
    // which would otherwise resolve that conflicting private resource.
    CHECK(run(state, "disabled={};Data={Records={originalActor,{Name=" + q(t.table.actor) + ",Type='Other',Parms='DNAFile=foreign.dnax;'}}};foreign=Data.Records[2]"));
    CHECK(patch(state, customToolActorPatch({t})));
    CHECK(run(state, "assert(#Data.Records==2 and Data.Records[2]==foreign and foreign.Parms=='DNAFile=foreign.dnax;');assert(#disabled==1 and disabled[1]==1000900)"));
    CHECK(run(state, "disabled={};DataMap={Actor={tool_zurgbossblaster=originalDependencies,[" + q(t.table.actor) + "]={Files={'foreign'}}}};foreign=DataMap.Actor[" + q(t.table.actor) + "]"));
    CHECK(patch(state, customToolDependenciesPatch({t})));
    CHECK(run(state, "assert(DataMap.Actor[" + q(t.table.actor) + "]==foreign and foreign.Files[1]=='foreign');assert(#disabled==1 and disabled[1]==1000900)"));
    CHECK(run(state, "disabled={};weapon={nativeWeapon,{name=" + q(t.privateWeapon) + ",class='gun',projectile='Foreign'}};proj={nativeShot};foreign=weapon[2]"));
    CHECK(patch(state, customToolWeaponPatch({t})));
    CHECK(run(state, "assert(#weapon==2 and #proj==1 and weapon[2]==foreign and foreign.projectile=='Foreign');assert(#disabled==1 and disabled[1]==1000900)"));

    // Tools without authored icons inherit their base tool's icon. An authored
    // icon keeps its own ID, and wrapping twice leaves a single native call.
    auto authored = tools[1]; authored.icon = "ui/icons/custom";
    CHECK(run(state, "iconCalls=0;ToolSelect={GetIconPath=function(self,name)iconCalls=iconCalls+1;assert(self==ToolSelect);return 'ui/icons/'..name,17 end}"));
    CHECK(patch(state, customToolIconPatch({t, authored}))); CHECK(patch(state, customToolIconPatch({t, authored})));
    CHECK(run(state, "local path,value=ToolSelect:GetIconPath(" + q(t.table.id) + ");assert(path=='ui/icons/ITEM_BLASTER' and value==17 and iconCalls==1);path=ToolSelect:GetIconPath(" + q(authored.table.id) + ");assert(path=='ui/icons/'.." + q(authored.table.id) + " and iconCalls==2);path=ToolSelect:GetIconPath('ITEM_NATIVE');assert(path=='ui/icons/ITEM_NATIVE' and iconCalls==3)"));
    taggedDna(state, tools, assets);
    lua_close(state);
}
}
int main(int argc,char** argv) {
    try { Fixture fixture; manifests(fixture.root / "manifests"); preparation(fixture.root / "prepare"); runtime(fixture.root / "runtime"); }
    catch (const std::exception& error) { std::printf("EXCEPTION: %s\n", error.what()); ++failures; }
    if(argc==4) try {
        std::vector<std::string> messages;
        auto index=crabe::mods::Index::load(fs::path(argv[2]).wstring(),messages);
        auto figures=loadCustomFigures(index.mods,messages);
        auto tools=loadCustomTools(index.mods,messages);
        auto assets=crabe::mods::AssetIndex::build(fs::path(argv[1]).wstring(),fs::path(argv[3]).wstring()+L"/cache",index.mods,messages);
        prepareCustomAssetDirectories(assets,figures,messages);
        prepareCustomTools(assets,fs::path(argv[1]).wstring(),tools,figures,messages);
        prepareCustomCharacterGameplay(assets,fs::path(argv[1]).wstring(),figures,messages);
        CHECK(tools.size()==1&&figures.size()==1);
        const fs::path output(argv[3]);
        write(output/"tool-actors.lua",customToolActorPatch(tools));
        write(output/"tool-dependencies.lua",customToolDependenciesPatch(tools));
        write(output/"tool-weapons.lua",customToolWeaponPatch(tools));
        write(output/"tool-icons.lua",customToolIconPatch(tools));
        write(output/"character-actors.lua",customGameplayActorPatch(figures));
        write(output/"character-dependencies.lua",customGameplayDependenciesPatch(figures));
        if(!tools.empty()) write(output/"tool-dna.lua",customToolDnaPatch(tools[0]));
        if(!figures.empty()) write(output/"character-dna.lua",customGameplayDnaPatch(figures[0]));
        for(const auto& m:assets.members) if(m.generated) {std::string bytes;CHECK(assets.read(m,bytes));write(output/"assets"/m.name,bytes);}
        for(const auto& message:messages) std::puts(message.c_str());
    } catch(const std::exception& error) {std::printf("PROBE EXCEPTION: %s\n",error.what());++failures;}
    std::printf("Standalone custom tool checks: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
