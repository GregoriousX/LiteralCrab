// SPDX-License-Identifier: GPL-3.0-only
#include "di1/lua_data.hpp"
#include "di1/native_catalog.hpp"
#include "di1/scene_format.hpp"
#include <cstdio>
#include <filesystem>
#include <stdexcept>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace crabe::di1;
namespace fs = std::filesystem;
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static std::string compile(const std::string& source) {
    auto* state = luaL_newstate();
    std::string binary;
    if (luaL_loadbuffer(state, source.data(), source.size(), "=fixture") == 0)
        lua_dump(state, [](lua_State*, const void* data, size_t size, void* out) {
            static_cast<std::string*>(out)->append(static_cast<const char*>(data), size); return 0; }, &binary);
    lua_close(state);
    return binary;
}
static bool rejects(const std::string& chunk) {
    try { data::evaluate(chunk); } catch (const std::exception&) { return true; }
    return false;
}

static void evaluator() {
    const std::string source = "DNA={AgentGadgets={Identification={ActorTypeMask='AV_Ferb',Name=\"PNF\\_\"},"
        "EntityInstance={ModelName=[[characters/PNF_Ferb/PNF_Ferb]]},TargetMode={TargetingNormalOffset={-0.9,2,0}}},"
        "Components={ToolSetManager={}},[1]=true,[true]='flag',Scale=1.5e0,Hex=0x10,Skip=nil;} -- comment\n"
        "Data = {Records={{Name='butch',Type='NPC'},{Name='av_zurg',Type='Avatar'}}}\n--[[ block ]]\nreturn DNA\n";
    for (const auto& chunk : {source, compile(source)}) {
        const auto globals = data::evaluate(chunk);
        const auto* dna = globals.table("DNA");
        CHECK(dna && dna->table("Components") && dna->table("Components")->table("ToolSetManager"));
        if (!dna) continue;
        CHECK(data::path(*dna, {"AgentGadgets", "Identification", "ActorTypeMask"})->string == "AV_Ferb");
        CHECK(data::path(*dna, {"AgentGadgets", "Identification", "Name"})->string == "PNF_");
        CHECK(data::path(*dna, {"AgentGadgets", "EntityInstance", "ModelName"})->string == "characters/PNF_Ferb/PNF_Ferb");
        const auto* offset = data::path(*dna, {"AgentGadgets", "TargetMode", "TargetingNormalOffset"});
        CHECK(offset && offset->table->indices.at(1).number == -0.9 && offset->table->indices.at(3).number == 0);
        CHECK(dna->indices.at(1).boolean && dna->flags.at(true).string == "flag" && dna->field("Scale")->number == 1.5);
        CHECK(dna->field("Hex")->number == 16 && !dna->field("Skip"));
        const auto* records = data::path(globals, {"Data", "Records"});
        CHECK(records && records->table->indices.size() == 2 && records->table->indices.at(2).table->text("Name") == "av_zurg");
        // The literal re-reads to the same value through the reference interpreter.
        auto* state = luaL_newstate(); luaL_openlibs(state);
        const auto check ="local v=" + data::literal(globals.fields.at("DNA")) +
            " assert(v.AgentGadgets.TargetMode.TargetingNormalOffset[1]==-0.9 and v[true]=='flag' and v.Hex==16)";
        CHECK(luaL_dostring(state, check.c_str()) == 0);
        lua_close(state);
    }
    // More than 50 array items use SETLIST blocks.
    std::string list = "List={";
    for (int i = 1; i <= 120; ++i) list += std::to_string(i) + ",";
    const auto large = data::evaluate(compile(list + "}"));
    CHECK(large.table("List")->indices.size() == 120 && large.table("List")->indices.at(120).number == 120);
    for (const char* code : {"DNA={a=1+2}", "local x=1 DNA={}"}) CHECK(rejects(code));
    // Compiled chunks reject code, calls and unknown globals; folded constants are data.
    for (const char* code : {"DNA=f()", "DNA={a=b}", "DNA={a='x'..'y'}", "for i=1,2 do end"}) {
        CHECK(rejects(code));
        const auto binary = compile(code);
        CHECK(!binary.empty() && rejects(binary));
    }
    CHECK(rejects("DNA={a=1} return DNA DNA={}") && rejects(std::string("\x1bLua\x51\0\1\4\4\4\4\0", 12)));
}

static void scenes() {
    scene::Scene s;
    s.strings = {"", "Root", "SceneFilename", "a.oct", "FileSets", "FileSet", "Models"};
    s.atoms = {{0, 0x01, 1, {}}, {1, 0x0B, 2, {3}}, {1, 0x01, 4, {}}, {2, 0x05, 5, {6}}, {2, 0x12, 5, {1, 2, 0x3f800000}}};
    const auto bytes = scene::encode(s);
    const auto decoded = scene::decode(bytes);
    CHECK(scene::encode(decoded) == bytes && decoded.atoms.size() == 5);
    CHECK(scene::text(decoded, decoded.atoms[1]) == "a.oct" && scene::children(decoded, 2).size() == 2);
    auto changed = decoded;
    scene::setText(changed, changed.atoms[1], "../b/b.oct");
    const auto again = scene::decode(scene::encode(changed));
    CHECK(scene::text(again, again.atoms[1]) == "../b/b.oct" && again.strings.size() == s.strings.size() + 1);
    CHECK(scene::subtreeEnd(again, 2) == 5 && scene::child(again, 0, "FileSets") == 2);
    auto bad = bytes; bad[20] = 9; // Atom count.
    bool threw{}; try { scene::decode(bad); } catch (const std::exception&) { threw = true; } CHECK(threw);
    CHECK(joinPath("characters/lr_butch", "../biped/motions/A.oct") == "characters/biped/motions/a.oct");
    CHECK(relativePath("characters/lr_butch", "characters/lr_loneranger/motions/x.oct") == "../lr_loneranger/motions/x.oct");
    CHECK(relativePath("characters/lr_butch", "characters/lr_butch/lr_butch.oct") == "lr_butch.oct");
}

// Optional: every shipped DNA/behavior/data chunk and a sample of scenes.
static void original(const fs::path& assets) {
    crabe::mods::AssetIndex none;
    NativeCatalog catalog(none, assets.wstring());
    unsigned chunks{}, scenesChecked{};
    for (const auto* archive : {"characters/characters.zip", "gamedb/core/core.zip"}) {
        const auto zip = catalog.archive(archive);
        CHECK(zip != nullptr);
        if (!zip) continue;
        for (const auto& entry : zip->entries()) {
            const auto name = lowerPath(entry.name);
            if (!name.ends_with(".dnax") && !name.ends_with(".btx") && !name.ends_with(".bmx") && !name.ends_with(".lua")) continue;
            const auto bytes = zip->read(entry);
            if (bytes.substr(0, 4) != std::string("\x1bLua", 4)) continue; // Core helper scripts are code.
            try { data::evaluateBinary(bytes); ++chunks; }
            catch (const std::exception& error) { std::printf("FAIL chunk %s: %s\n", name.c_str(), error.what()); ++failures; }
        }
    }
    const auto* butch = catalog.actor("butch");
    CHECK(butch && butch->type == "NPC" && lowerPath(butch->dna) == "characters/butch.dnax");
    const auto* phineas = catalog.actor("PNF_Phineas");
    CHECK(phineas && phineas->type == "Avatar");
    CHECK(catalog.dependencies("lr_loneranger") && catalog.dependencies("tool_zurgbossblaster"));
    const auto& ferb = catalog.chunk("characters/PNF_Ferb.dnax");
    CHECK(data::path(ferb, {"DNA", "AgentGadgets", "Identification", "ActorTypeMask"})->string == "AV_Ferb");
    const auto tools = catalog.tools();
    CHECK(tools.profile("LR_Butch") && !tools.profile("AV_Ferb") && tools.find("ITEM_BLASTER")->playerControls);
    CHECK(tools.find("ITEM_ZURG_BOSS_BLASTER") && !tools.find("ITEM_ZURG_BOSS_BLASTER")->playerControls);
    CHECK(tools.find("ITEM_BLASTER")->actors == std::vector<std::string>{"Tool_Blaster"});
    try {
        const auto* sets = data::path(catalog.chunk("gamedb/core/bitmasks.lua"), {"Data", "Sets"});
        CHECK(sets && sets->isTable() && sets->table->indices.size() > 30);
        // Player-controllable actor types: AV_Zurg is one, the cut AV_Ferb is not defined.
        bool zurgType{}, ferbType{};
        if (sets) for (const auto& [_, set] : sets->table->indices)
            if (const auto* filters = set.isTable() ? set.table->table("filters") : nullptr)
                for (const auto& [__, filter] : filters->indices)
                    if (filter.isTable() && filter.table->text("filter") == "PlayerControllable") {
                        zurgType = filter.table->text("desc").find("AV_Zurg") != std::string::npos;
                        ferbType = filter.table->text("desc").find("AV_Ferb") != std::string::npos;
                    }
        CHECK(zurgType && !ferbType);
    } catch (const std::exception& error) { std::printf("FAIL bitmasks: %s\n", error.what()); ++failures; }
    const auto* weapon = catalog.weapons().table("weapon");
    CHECK(weapon && weapon->indices.size() > 100);
    for (const auto* model : {"characters/lr_butch/lr_butch.zip", "characters/lr_loneranger/lr_loneranger.zip",
        "characters/pnf_phineas/pnf_phineas.zip", "characters/ts_zurg/ts_zurg.zip"}) {
        for (const auto& [name, entry] : catalog.members(model)) {
            if (!name.ends_with(".bent") && !name.ends_with(".banm") && !name.ends_with(".oct")) continue;
            const auto bytes = entry.archive->read(entry.archive->entries()[entry.entry]);
            try { CHECK(scene::encode(scene::decode(bytes)) == bytes); ++scenesChecked; }
            catch (const std::exception& error) { std::printf("FAIL scene %s: %s\n", name.c_str(), error.what()); ++failures; }
        }
    }
    std::printf("Original data: %u chunks evaluated, %u scenes round-tripped.\n", chunks, scenesChecked);
}

int main(int argc, char** argv) {
    try {
        evaluator(); scenes();
        if (argc == 2) original(argv[1]);
    } catch (const std::exception& error) {
        std::printf("FAIL: uncaught %s\n", error.what()); ++failures;
    }
    std::printf("Native data tests: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
