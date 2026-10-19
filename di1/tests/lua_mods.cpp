// SPDX-License-Identifier: GPL-3.0-only
// The mod runtime driven by the reference Lua 5.1.5 C API, standing in for the game's loader.
#include "mods/lua_mods.hpp"
#include "di1/character_freedom_data.hpp"
#include "di1/character_freedom_policy.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

using namespace crabe::mods;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static std::string logText;
static crabe::di1::CharacterPlaysets characterPlaysets;
static int __cdecl registeredAvatar(lua_State* state) {
    std::size_t size{};
    const auto* name = lua_tolstring(state, 1, &size);
    lua_pushboolean(state, name && characterPlaysets.registered({name, size}));
    return 1;
}
static unsigned registeredPlaysets{};
static int __cdecl registerPlayset(lua_State* state) {
    std::size_t nameSize{}, listSize{};
    const auto* name = lua_tolstring(state, 1, &nameSize);
    const auto* list = lua_tolstring(state, 2, &listSize);
    CHECK(name && list);
    if (name && list) { characterPlaysets.add({name, nameSize}, {list, listSize}); ++registeredPlaysets; }
    return 0;
}
static void capture(const char* format, ...) {
    char line[2048];
    va_list args;
    va_start(args, format);
    vsnprintf_s(line, _TRUNCATE, format, args);
    va_end(args);
    std::printf("  log: %s\n", line);
    logText += line;
    logText += '\n';
}

// A stand-in engine adapter: Award(player, name[, force]) -> player == 2 and force.
static int __cdecl award(lua_State* state) {
    const bool ok = lua_type(state, 1) == LUA_TNUMBER && lua_tonumber(state, 1) == 2 &&
        lua_type(state, 2) == LUA_TSTRING && lua_toboolean(state, 3);
    lua_pushboolean(state, ok);
    return 1;
}

static void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(text.data(), static_cast<std::streamsize>(text.size()));
}

// Loads like the game does, then runs the chunk; returns the pcall status.
static int run(lua_State* state, std::string_view source, const char* name, int results = 1) {
    const int top = lua_gettop(state);
    const int status = loadBuffer(state, source.data(), source.size(), name);
    if (status) return status;
    CHECK(lua_gettop(state) == top + 1);
    return lua_pcall(state, 0, results, 0);
}

static std::string resultString(lua_State* state) {
    std::size_t length{};
    const char* text = lua_tolstring(state, -1, &length);
    return text ? std::string(text, length) : "<nil>";
}

static bool evaluate(lua_State* state, const char* expression) {
    const std::string source = std::string("return ") + expression;
    if (luaL_loadbuffer(state, source.data(), source.size(), "=check") || lua_pcall(state, 0, 1, 0)) {
        std::printf("  check error: %s\n", lua_tostring(state, -1));
        lua_settop(state, 0);
        return false;
    }
    const bool result = lua_toboolean(state, -1) != 0;
    lua_settop(state, 0);
    return result;
}

int main() {
    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const fs::path root = fs::path(temp) / (L"literalcrab-luamods-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);
    write(root / "demo/mod.ini",
        "[Mod]\nName=Demo\n[Options]\nItem=extra\nCount=2\n"
        "[Override]\nscripts/replaced.lua=replaced.lua\nscripts/broken.lua=broken.lua\n"
        "[Patch]\nscripts/menu.lua=menu.lua\nscripts/menu.lua=menu_second.lua\nscripts/env.lua=env.lua\n"
        "scripts/fails.lua=fails.lua\nscripts/syntax.lua=syntax.lua\nscripts/multi.lua=noop.lua\n"
        "scripts/error.lua=noop.lua\nscripts/compiled.lua=compiled.lua\nreplaced.lua=after_replace.lua\n"
        "contains:UNNAMED_MARKER=unnamed.lua\nscripts/data.lua=data.lua\nscripts/adapter.lua=adapter.lua\n");
    write(root / "demo/adapter.lua",
        "local _, _, log = ...\n"
        "log(string.format('adapter %s %s %s %s', tostring(LiteralCrab.has('Award')), tostring(LiteralCrab.has('Missing')),\n"
        "    tostring(LiteralCrab.has('has')), tostring(LiteralCrab.Award(2, 'ITEM_X', true))))\n"
        "AdapterResult = LiteralCrab.Award(0, 'bad')");
    write(root / "demo/data.lua",
        "local _, options, log = ...\n"
        "local keys = {}\nfor key in pairs(Data) do if type(key) == 'string' and key ~= 'list' then table.insert(keys, key) end end\n"
        "table.sort(keys)\ntable.sort(Data.list)\nlocal sorted = {}\nfor _, value in ipairs(Data.list) do sorted[#sorted + 1] = tostring(value) end\n"
        "log(string.format('keys %s; sorted %s; floor %d; %s', table.concat(keys, ','), table.concat(sorted, ' '),\n"
        "    math.floor(2.7), io == nil and dofile == nil and 'no io' or 'io leaked'))\n"
        "Data.patched = true");
    write(root / "demo/unnamed.lua", "UnnamedPatched = Marker");
    write(root / "demo/replaced.lua", "Replaced = true\nreturn 'mod'");
    write(root / "demo/broken.lua", "return (");
    write(root / "demo/menu.lua",
        "local menu, options, log = ...\nfor i = 1, options.Count do table.insert(menu.items, options.Item) end\nlog('added')");
    write(root / "demo/menu_second.lua", "local menu = ...\nmenu.second = #menu.items");
    write(root / "demo/env.lua", "local _, _, _, environment = ...\nY = X + 1\nSeenX = rawget(environment, 'X')");
    write(root / "demo/fails.lua", "error('boom')");
    write(root / "demo/syntax.lua", "this is not lua");
    write(root / "demo/noop.lua", "Patched = (Patched or 0) + 1");
    write(root / "demo/compiled.lua", "local value = ...\nCompiledPatched = value");
    write(root / "demo/after_replace.lua", "AfterReplace = ...");
    std::vector<std::string> messages;
    auto index = Index::load(root, messages);
    for (const auto& message : messages) std::printf("  %s\n", message.c_str());
    CHECK(index.mods.size() == 1);

    LuaApi api{};
    api.loadBuffer = &luaL_loadbuffer;
    api.getTop = &lua_gettop;
    api.setTop = &lua_settop;
    api.pushValue = &lua_pushvalue;
    api.insert = &lua_insert;
    api.pushClosure = &lua_pushcclosure;
    api.pcall = &lua_pcall;
    api.toLString = &lua_tolstring;
    api.toNumber = &lua_tonumber;
    api.toBoolean = &lua_toboolean;
    api.pushBoolean = &lua_pushboolean;
    api.type = &lua_type;
    // Invalid adapter names are never exposed.
    api.adapters = {{"Award", &award}, {"has", &award}, {"1bad", &award}, {"bad name", &award}};

    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    // Every C function of the reference libraries, as an engine profile would list them;
    // configure() keeps only its allowlist.
    for (const char* library : {"_G", "table", "string", "math"}) {
        lua_getglobal(state, library);
        const std::string prefix = std::strcmp(library, "_G") ? std::string(library) + "." : "";
        for (lua_pushnil(state); lua_next(state, -2); lua_pop(state, 1))
            if (lua_type(state, -2) == LUA_TSTRING && lua_iscfunction(state, -1))
                api.library.push_back({prefix + lua_tostring(state, -2), lua_tocfunction(state, -1)});
        lua_pop(state, 1);
    }
    CHECK(api.library.size() > 60);
    {
        LuaApi incomplete = api;
        incomplete.library.clear();
        CHECK(!configure(incomplete, Index{}, &capture, false));
    }
    CHECK(configure(api, std::move(index), &capture, false));

    // Untargeted chunks pass through.
    CHECK(run(state, "return 'game'", "@scripts/other.lua") == 0 && resultString(state) == "game");
    lua_settop(state, 0);

    // Override replaces the source; patches on the same chunk still compose afterwards.
    CHECK(run(state, "return 'game'", "@Scripts\\Replaced.lua") == 0 && resultString(state) == "mod");
    lua_settop(state, 0);
    CHECK(evaluate(state, "Replaced == true and AfterReplace == 'mod'"));

    // A broken override falls back to the game chunk.
    CHECK(run(state, "return 'game'", "@scripts/broken.lua") == 0 && resultString(state) == "game");
    lua_settop(state, 0);
    CHECK(logText.find("mods/demo/broken.lua failed to compile; the game chunk is used instead") != std::string::npos);

    // Patches see the first result, typed options and the log; they compose in manifest order.
    CHECK(run(state, "Menu = {items = {'a'}}\nreturn Menu", "@scripts/menu.lua") == 0);
    lua_settop(state, 0);
    CHECK(evaluate(state, "#Menu.items == 3 and Menu.items[3] == 'extra' and Menu.second == 3"));
    CHECK(logText.find("Mod demo/menu.lua: added") != std::string::npos);

    // The environment the game assigns to the chunk is shared with the patch.
    {
        const std::string source = "X = 1";
        CHECK(loadBuffer(state, source.data(), source.size(), "@scripts/env.lua") == 0);
        lua_newtable(state);
        lua_setfenv(state, -2);
        lua_getfenv(state, -1);
        lua_insert(state, -2);
        CHECK(lua_pcall(state, 0, 0, 0) == 0);
        lua_getfield(state, -1, "Y");
        CHECK(lua_tonumber(state, -1) == 2);
        // The fourth argument is that environment itself, for walking the chunk's globals.
        lua_getfield(state, -2, "SeenX");
        CHECK(lua_tonumber(state, -1) == 1);
        lua_settop(state, 0);
        CHECK(evaluate(state, "X == nil and Y == nil"));
    }

    // A failing patch is reported and never alters the chunk's results.
    CHECK(run(state, "return 'kept'", "@scripts/fails.lua") == 0 && resultString(state) == "kept");
    lua_settop(state, 0);
    CHECK(logText.find("Mod demo/fails.lua: FAILED:") != std::string::npos && logText.find("boom") != std::string::npos);

    // A patch that does not compile leaves the game chunk exactly as compiled.
    CHECK(run(state, "return 'intact'", "@scripts/syntax.lua") == 0 && resultString(state) == "intact");
    lua_settop(state, 0);
    CHECK(logText.find("Mod mods/demo/syntax.lua failed to compile") != std::string::npos);

    // Multiple results including nils are preserved; arguments still reach the chunk.
    {
        const std::string source = "local a, b = ...\nreturn a, nil, b, nil";
        CHECK(loadBuffer(state, source.data(), source.size(), "@scripts/multi.lua") == 0);
        lua_pushinteger(state, 7);
        lua_pushinteger(state, 9);
        CHECK(lua_pcall(state, 2, LUA_MULTRET, 0) == 0);
        CHECK(lua_gettop(state) == 4 && lua_tointeger(state, 1) == 7 && lua_isnil(state, 2) &&
            lua_tointeger(state, 3) == 9 && lua_isnil(state, 4));
        lua_settop(state, 0);
    }

    // The chunk's own errors propagate unchanged and the patch does not run.
    CHECK(run(state, "error('original', 0)", "@scripts/error.lua") != 0 && resultString(state) == "original");
    lua_settop(state, 0);
    CHECK(evaluate(state, "Patched == 1"));

    // Precompiled chunks are matched by the source name inside the bytecode.
    {
        const std::string source = "return string.dump(loadstring('return 42', '@Scripts/Compiled.lua'))";
        CHECK(luaL_loadbuffer(state, source.data(), source.size(), "=dump") == 0 && lua_pcall(state, 0, 1, 0) == 0);
        std::size_t length{};
        const char* bytes = lua_tolstring(state, -1, &length);
        const std::string bytecode(bytes, length);
        lua_settop(state, 0);
        CHECK(run(state, bytecode, "=packed chunk") == 0 && lua_tointeger(state, -1) == 42);
        lua_settop(state, 0);
        CHECK(evaluate(state, "CompiledPatched == 42"));
    }

    // Unnamed chunks are matched by content; named chunks with the same text are not.
    CHECK(run(state, "Marker = 'UNNAMED_MARKER'", "") == 0);
    lua_settop(state, 0);
    CHECK(evaluate(state, "UnnamedPatched == 'UNNAMED_MARKER'"));
    CHECK(run(state, "UnnamedPatched = nil Marker = 'UNNAMED_MARKER'", "@scripts/named.lua") == 0);
    lua_settop(state, 0);
    CHECK(evaluate(state, "UnnamedPatched == nil"));

    // The game loads data tables into states without any library; patches still get lib.
    {
        lua_State* bare = luaL_newstate();
        // Adapters are reachable from every patch, even in a state without any library, and
        // nothing is added to the game's globals.
        CHECK(run(bare, "AdapterResult = nil", "@scripts/adapter.lua") == 0);
        lua_settop(bare, 0);
        CHECK(logText.find("adapter.lua: adapter true false false true") != std::string::npos);
        lua_getglobal(bare, "AdapterResult");
        CHECK(lua_isboolean(bare, -1) && !lua_toboolean(bare, -1));
        lua_settop(bare, 0);
        lua_getglobal(bare, "LiteralCrab");
        CHECK(lua_isnil(bare, -1));
        lua_settop(bare, 0);
        CHECK(run(bare, "Data = {b = 2, a = 1, list = {3, 1, 2}}", "@scripts/data.lua") == 0);
        lua_settop(bare, 0);
        CHECK(logText.find("data.lua: keys a,b; sorted 1 2 3; floor 2; no io") != std::string::npos);
        lua_getglobal(bare, "Data");
        lua_getfield(bare, -1, "patched");
        CHECK(lua_toboolean(bare, -1));
        lua_settop(bare, 0);
        // Globals the patch did not define stay absent from the game's state.
        lua_getglobal(bare, "pairs");
        CHECK(lua_isnil(bare, -1));
        lua_settop(bare, 0);
        lua_close(bare);
    }

    CHECK(lua_gettop(state) == 0);
    // Exercise the actual built-in patch through the game loader API, including a bare data
    // state. Membership must remain byte-for-byte native, even with an earlier mod's patch.
    {
        Index freedom;
        freedom.addBuiltinPatch({"zonelist.lua", "contains:PlayableAvatars"}, "character_playsets",
            std::string(crabe::di1::characterPlaysetsPatch), "{}");
        freedom.addBuiltinPatch({"contains:ActorList"}, "character_actor_registry",
            std::string(crabe::di1::characterToyboxActorsPatch), "{}");
        freedom.addBuiltinPatch({"monstersu_actors.lua", "contains:mu_randall_ps"}, "character_native_home_MonstersU",
            std::string(crabe::di1::characterNativeActorHomesPatch), "{Playset='MonstersU'}");
        api.adapters.push_back({"RegisterPlaysetAvatars", &registerPlayset});
        CHECK(configure(api, std::move(freedom), &capture, false));
        const char* source =
            "ZoneList={Records={PlaysetProperties={"
            "Incredibles={PlayableAvatars='AV_MrIncredible,AV_Dash',avatarSuffix='_PS',ComboPointsMeter='MeleeMeter',DisableVehicleToolSelect=true},"
            "Pirates={PlayableAvatars='PIR_JackSparrow,PIR_Barbossa',avatarSuffix='_PS',DisableVehicleToolSelect=true},"
            "RumpusRoom={PlayableAvatars='Mod_ExistingAvatar',WantToyBoxFX=true,DisableVehicleToolSelect=true},"
            "Custom={PlayableAvatars='Mod_Character',WantToyBoxFX=false,DisableVehicleToolSelect=true}}}}";
        lua_State* bare = luaL_newstate();
        // Packed/unnamed data takes the content matcher used by the real zone list.
        CHECK(run(bare, source, "") == 0);
        lua_settop(bare, 0);
        CHECK(registeredPlaysets == 3);
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.PlayableAvatars == 'AV_MrIncredible,AV_Dash'"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Pirates.PlayableAvatars == 'PIR_JackSparrow,PIR_Barbossa'"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.avatarSuffix == '_PS'"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.ComboPointsMeter == 'MeleeMeter'"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.WantToyBoxFX == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Pirates.WantToyBoxFX == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.WantBloatFX == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Pirates.WantBloatFX == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.ShowCarEnergyMeterUI == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Pirates.ShowCarEnergyMeterUI == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Custom.ShowCarEnergyMeterUI == nil"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Custom.WantToyBoxFX == false"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.DisableVehicleToolSelect == false"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Pirates.DisableVehicleToolSelect == true and ZoneList.Records.PlaysetProperties.RumpusRoom.DisableVehicleToolSelect == true and ZoneList.Records.PlaysetProperties.Custom.DisableVehicleToolSelect == true"));
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.RumpusRoom.PlayableAvatars == 'Mod_ExistingAvatar'"));
        CHECK(evaluate(bare, "LiteralCrab == nil and pairs == nil"));
        CHECK(characterPlaysets.foreign("PIR_JackSparrow", "Incredibles"));
        CHECK(!characterPlaysets.foreign("AV_MrIncredible", "Incredibles"));
        CHECK(run(bare, source, "@GameDB/Core/ZoneList.lua") == 0);
        lua_settop(bare, 0);
        CHECK(registeredPlaysets == 6);
        CHECK(evaluate(bare, "ZoneList.Records.PlaysetProperties.Incredibles.PlayableAvatars == 'AV_MrIncredible,AV_Dash'"));
        // A malformed/absent table is a no-op, not a Lua error or a progression rewrite.
        CHECK(run(bare, "ZoneList={Records={PlaysetProperties=false}}", "@zonelist.lua") == 0);
        lua_settop(bare, 0);
        CHECK(registeredPlaysets == 6);
        const char* actors = "Data={Type='ActorList',Records={"
            "{Type='Avatar',Name='PNF_Perry'},"
            "{Type='Avatar',Name='FRO_Elsa'},"
            "{Type='Avatar',Name='AV_MrIncredible'},"
            "{Type='NPC',Name='StoryNPC'},"
            "{Type='Avatar',Name=false}}}";
        CHECK(run(bare, actors, "") == 0);
        lua_settop(bare, 0);
        CHECK(registeredPlaysets == 9);
        CHECK(characterPlaysets.foreign("PNF_Perry", "Incredibles"));
        CHECK(characterPlaysets.foreign("FRO_Elsa", "Pirates"));
        CHECK(!characterPlaysets.foreign("AV_MrIncredible", "Incredibles"));
        CHECK(!characterPlaysets.foreign("StoryNPC", "Incredibles"));
        CHECK(run(bare, "Data={Type='Unrelated',Records={{Type='Avatar',Name='ActorList'}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(registeredPlaysets == 9);
        lua_close(bare);
        // Native _PS records restore ownership omitted from the PC roster.
        bare = luaL_newstate();
        CHECK(run(bare, "Data={Type='ActorList',Records={{Type='Avatar',Name='mu_randall_ps'},{Type='Avatar',Name='mu_sullyinfinite_ps'},{Type='NPC',Name='StoryNPC_PS'}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(!characterPlaysets.foreign("MU_Randall", "MonstersU"));
        CHECK(characterPlaysets.foreign("MU_Randall", "Pirates"));
        CHECK(!characterPlaysets.foreign("MU_SullyInfinite", "MonstersU"));
        CHECK(!characterPlaysets.registered("StoryNPC"));
        CHECK(evaluate(bare, "Data.Records[1].Name == 'mu_randall_ps' and Data.Records[3].Type == 'NPC'"));
        lua_close(bare);
    }
    {
        Index preloads;
        preloads.addBuiltinPatch({"contains:_loadforallplaysets_", "contains:avatarstats"}, "character_preloads",
            std::string(crabe::di1::characterPreloadsPatch), "{}");
        api.adapters.push_back({"IsRegisteredAvatar", &registeredAvatar});
        CHECK(configure(api, std::move(preloads), &capture, false));
        auto* bare = luaL_newstate();
        const char* coreMap = "DataMap={Playset={_loadforallplaysets_={Files={'Screen|HUD'},Includes={'_proxyavatar|Actor'}}}}";
        CHECK(run(bare, coreMap, "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Playset._loadforallplaysets_.Includes == 4"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Includes[4] == 'avatartools|Include'"));
        CHECK(run(bare, "DataMap=DataMap -- _loadforallplaysets_ repeated", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Playset._loadforallplaysets_.Includes == 4"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[1] == 'Screen|HUD'"));
        CHECK(evaluate(bare, "#DataMap.Playset._loadforallplaysets_.Files == 13"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[10] == 'Audio|RumpusVehicles.fev'"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[11] == 'Audio|RumpusCarBody.fev'"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[12] == 'Audio|VehicleEngine.fev'"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[13] == 'Audio|CARS_TBScript.dlg'"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[3] == 'GameDB|GameDB/ToyBox/cust_Avatar__lua.lua|1|cust'"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Includes[1] == '_proxyavatar|Actor'"));
        CHECK(run(bare, "DataMap={Playset={_loadforallplaysets_={Files={'Screen|HUD','GAMEDB|GAMEDB/CORE/CUST_AVATAR__LUA.LUA|1|CUST'}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Playset._loadforallplaysets_.Files == 13"));
        CHECK(evaluate(bare, "DataMap.Playset._loadforallplaysets_.Files[2] == 'GAMEDB|GAMEDB/CORE/CUST_AVATAR__LUA.LUA|1|CUST'"));
        CHECK(run(bare, "DataMap={Playset={_loadforallplaysets_={Files={'AUDIO|RUMPUSVEHICLES.FEV'}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Playset._loadforallplaysets_.Files == 12 and DataMap.Playset._loadforallplaysets_.Files[1] == 'AUDIO|RUMPUSVEHICLES.FEV'"));
        const char* avatarMap = "DataMap={Include={"
            "avatarstats={Files={'GameDB|GameDB/AV_Perry/stats.lua|1|stats'}},"
            "perry_inc={Files={'Cho|Swing','CHO|SWING','Script|Behavior'},Includes={'shared|Include'}},"
            "shared={Files={'Cho|Shared'},Includes={'perry_inc|Include'}},"
            "avatartools={Includes={'hat|Actor'}}},Actor={"
            "pnf_perry={Files={'DNA|Perry','Entity|PerryModel'},Includes={'perry_inc|Include'}},"
            "StoryNPC={Files={'Cho|NPC'}},hat={Files={'Cho|Hat','Entity|HatModel'}}}}";
        CHECK(run(bare, avatarMap, "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Include.literalcrab_avatar_fx.Files == 3"));
        CHECK(evaluate(bare, "DataMap.Actor.pnf_perry.Files[2] == 'Entity|PerryModel'"));
        CHECK(evaluate(bare, "#DataMap.Include.avatarstats.Files == 1"));
        CHECK(evaluate(bare, "DataMap.Include.avatartools.Includes[1] == 'hat|Actor' and DataMap.Actor.hat.Files[2] == 'Entity|HatModel'"));
        CHECK(run(bare, "DataMap=DataMap -- avatarstats repeated", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "#DataMap.Include.literalcrab_avatar_fx.Files == 3"));
        lua_close(bare);
    }
    {
        Index targets;
        targets.addBuiltinPatch({"lr_shoottargets.dnax", "contains:target_bullseye"}, "character_targets",
            std::string(crabe::di1::characterTargetsPatch), "{}");
        CHECK(configure(api, std::move(targets), &capture, false));
        auto* bare = luaL_newstate();
        const char* source = "DNA={AgentGadgets={Exploder={WeaponList='pstl|toma|modx',IgnoreWeaponList=false,"
            "ExplodeOnMunitionImpact=true,ExplodeOnActorCollisionAtSpeed=-1,WeaponFactor=0.7},"
            "Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='target_bullseye'}}}},Reward='Native_Reward'}";
        CHECK(run(bare, source, "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.Exploder.WeaponList == 'pstl|toma|modx|lctg'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.Exploder.IgnoreWeaponList == false and DNA.AgentGadgets.Exploder.ExplodeOnActorCollisionAtSpeed == -1 and DNA.Reward == 'Native_Reward'"));
        CHECK(run(bare, "Before=DNA.AgentGadgets.Exploder.WeaponList; DNA=DNA -- target_bullseye", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "Before == DNA.AgentGadgets.Exploder.WeaponList"));
        for (const char* list : {"pstl|toma|abcd|efgh|ijkl|mnop|qrst|uvwx", "pstl|toma|Mod_Ammo",
            "|pstl|toma", "pstl|toma|", "pstl||||||||||toma", "pstl|toma|xx\\000y"}) {
            const auto full = std::string("DNA={AgentGadgets={Exploder={WeaponList='") + list +
                "',IgnoreWeaponList=false,ExplodeOnMunitionImpact=true},Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='target_bullseye'}}}}}";
            CHECK(run(bare, full.c_str(), "") == 0);
            lua_settop(bare, 0);
            CHECK(evaluate(bare, (std::string("DNA.AgentGadgets.Exploder.WeaponList == '") + list + "'").c_str()));
        }
        CHECK(run(bare, "DNA={AgentGadgets={Exploder={WeaponList='pstl|toma|abcd|efgh|ijkl|mnop|qrst',IgnoreWeaponList=false,ExplodeOnMunitionImpact=true},Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='target_bullseye'}}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.Exploder.WeaponList == 'pstl|toma|abcd|efgh|ijkl|mnop|qrst|lctg'"));
        for (const char* weapon : {"lrcb", "lrgb", "pstl", "toma", "Mod_Ammo"}) {
            const auto specialSource = std::string("DNA={AgentGadgets={Exploder={WeaponList='") + weapon +
                "',IgnoreWeaponList=false,ExplodeOnMunitionImpact=true},Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='target_bullseye'}}}}}";
            CHECK(run(bare, specialSource.c_str(), "") == 0);
            lua_settop(bare, 0);
            CHECK(evaluate(bare, (std::string("DNA.AgentGadgets.Exploder.WeaponList == '") + weapon + "'").c_str()));
        }
        CHECK(run(bare, "DNA={AgentGadgets={Exploder={WeaponList='pstl|toma',IgnoreWeaponList=false,ExplodeOnMunitionImpact=true},Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='Other_Barrel'}}}}}", "@lr_shoottargets.dnax") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.Exploder.WeaponList == 'pstl|toma'"));
        CHECK(run(bare, "DNA={AgentGadgets={Exploder={WeaponList='pstl|toma',IgnoreWeaponList=true,ExplodeOnMunitionImpact=true},Effects={FX={{Type='SOUND',Event='ExploderExplode',Name='target_bullseye'}}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.Exploder.WeaponList == 'pstl|toma' and DNA.AgentGadgets.Exploder.IgnoreWeaponList == true"));
        lua_close(bare);
    }
    {
        Index mounts;
        mounts.addBuiltinPatch({"contains:AvatarInteractionHud", "contains:SummonMount"}, "character_mounts",
            std::string(crabe::di1::characterMountsPatch), "{}");
        CHECK(configure(api, std::move(mounts), &capture, false));
        auto* bare = luaL_newstate();
        const char* source = "DNA={AgentGadgets={Avatar={},Rider={},AvatarInteractionHud={},"
            "Identification={ActorTypeMask='AV_Perry',Name='PNF_PerryInfinite'},Health={MaxHealth=100}},"
            "Components={CCDB_GameModelDefinition={DefaultPrebuilt='Avatar_Perry_Exclusive'}}}";
        CHECK(run(bare, source, "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_HitchingPost[1].CreationKey == 'LR_Silver'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_HitchingPost[1].Lock == 'RC_TOY_LR_Silver'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_HitchingPost[2].Lock == 'RC_LR_Scout'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_MuleHitchingPost[1].Lock == 'RC_LR_Mule'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_ElephantHitchingPost[1].Lock == 'RC_LR_Elephant'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.TS_HitchingPost[1].CreationKey == 'AV_Bullseye'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.TS_AlienHorseHitchingPost[1].Lock == 'RC_ROBO_BULLSEYE'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.IN_HitchingPost[1].Lock == 'RC_IN_MRINC_CAR'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.IN_HelicopterHitchingPost[1].Lock == 'RC_TOY_SUPER_HERO_HELICOPTER'"));
        CHECK(evaluate(bare, "#DNA.AgentGadgets.SummonMount.TypedMounts.MU_HitchingPost == 4 and DNA.AgentGadgets.SummonMount.TypedMounts.MU_HitchingPost[4].Lock == 'RC_MU_BIKE_RANDY'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.Health.MaxHealth == 100 and DNA.Components.CCDB_GameModelDefinition.DefaultPrebuilt == 'Avatar_Perry_Exclusive'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.Mounts == nil and LiteralCrab == nil and pairs == nil"));
        CHECK(run(bare, "SavedMounts=DNA.AgentGadgets.SummonMount.TypedMounts; DNA=DNA -- AvatarInteractionHud", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "SavedMounts == DNA.AgentGadgets.SummonMount.TypedMounts and #SavedMounts.LR_HitchingPost == 2"));
        const char* native = "DNA={AgentGadgets={Avatar={},Rider={},Identification={ActorTypeMask='PNF_Perry'},"
            "SummonMount={Mounts={{CreationKey='LR_Scout',Lock='Original_Lock'}},"
            "TypedMounts={TS_HitchingPost={{CreationKey='Mod_Mount',Lock='Custom_Lock'}}}}}}";
        CHECK(run(bare, native, "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.Mounts[1].CreationKey == 'LR_Scout'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.LR_HitchingPost[1].Lock == 'Original_Lock'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.TS_HitchingPost[1].Lock == 'Custom_Lock'"));
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts.IN_HitchingPost[1].CreationKey == 'IN_MRINC_CAR'"));
        CHECK(run(bare, "DNA={AgentGadgets={Avatar={},Rider={},Identification={ActorTypeMask='PNF_Perry'},SummonMount={Mounts={{CreationKey='Mod_Unknown',Lock='Mod_Lock'}}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts == nil and DNA.AgentGadgets.SummonMount.Mounts[1].Lock == 'Mod_Lock'"));
        CHECK(run(bare, "DNA={AgentGadgets={Avatar={},Rider={},Identification={ActorTypeMask='PNF_Perry'},SummonMount={Mounts={{CreationKey='LR_Silver'},{CreationKey='IN_MRINC_CAR'}}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts == nil and #DNA.AgentGadgets.SummonMount.Mounts == 2"));
        for (const char* rejected : {
            "DNA={AgentGadgets={Avatar={},Rider={},AvatarInteractionHud={},Identification={ActorTypeMask='StoryNPC'}}}",
            "DNA={AgentGadgets={Avatar={},AvatarInteractionHud={},Identification={ActorTypeMask='PNF_Perry'}}}",
            "DNA={AgentGadgets={Rider={},AvatarInteractionHud={},Identification={ActorTypeMask='PNF_Perry'}}}",
            "DNA={AgentGadgets={Avatar={},Rider={},AvatarInteractionHud={},Identification={ActorTypeMask=false}}}"}) {
            CHECK(run(bare, rejected, "") == 0);
            lua_settop(bare, 0);
            CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount == nil"));
        }
        CHECK(run(bare, "DNA={AgentGadgets={Avatar={},Rider={},Identification={ActorTypeMask='PNF_Perry'},SummonMount={TypedMounts=false}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(evaluate(bare, "DNA.AgentGadgets.SummonMount.TypedMounts == false"));
        lua_close(bare);
    }
    {
        Index diagnostics;
        diagnostics.addBuiltinPatch({"contains:avatarstats"}, "character_dependency_trace",
            std::string(crabe::di1::characterDependenciesTracePatch), "{}");
        diagnostics.addBuiltinPatch({"contains:SPARKS_COLLECTED"}, "character_stats_trace",
            std::string(crabe::di1::characterStatsTracePatch), "{}");
        diagnostics.addBuiltinPatch({"contains:Customization"}, "character_customization_trace",
            std::string(crabe::di1::characterCustomizationTracePatch), "{}");
        CHECK(configure(api, std::move(diagnostics), &capture, false));
        auto* bare = luaL_newstate();
        CHECK(run(bare, "DataMap={Include={avatarstats={Files={'GameDB|GameDB/AV_Perry/stats.lua|1|stats'}}},"
            "Playset={incredibles={Includes={'avatarstats|Include'}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(logText.find("DEPENDENCIES: avatarstats has 1 files") != std::string::npos);
        CHECK(evaluate(bare, "DataMap.Include.avatarstats.Files[1] == 'GameDB|GameDB/AV_Perry/stats.lua|1|stats'"));
        CHECK(run(bare, "Data={Records={{Name='SPARKS_COLLECTED',Package='AV_Perry',ScopeUID=117,UID=173}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(logText.find("STAT LOADED: package=AV_Perry scope=117 uid=173") != std::string::npos);
        CHECK(evaluate(bare, "Data.Records[1].UID == 173 and Data.Records[1].ScopeUID == 117"));
        CHECK(run(bare, "Data={Type='Customization',Databases={{DBName='Avatar',Records={Prebuilt={{name='Avatar_Buzz_Exclusive',UID=123}}}}}}", "") == 0);
        lua_settop(bare, 0);
        CHECK(logText.find("AVATAR MODEL DEFINITION: Avatar_Buzz_Exclusive uid=123") != std::string::npos);
        CHECK(evaluate(bare, "Data.Databases[1].Records.Prebuilt[1].UID == 123"));
        lua_close(bare);
    }
    lua_close(state);
    fs::remove_all(root);
    if (failures) return 1;
    std::puts("PASS: script mod overrides, patches and failure isolation on Lua 5.1.5");
    return 0;
}
