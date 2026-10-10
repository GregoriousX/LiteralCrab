// SPDX-License-Identifier: GPL-3.0-only
// The mod runtime driven by the reference Lua 5.1.5 C API, standing in for the game's loader.
#include "mods/lua_mods.hpp"
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
    lua_close(state);
    fs::remove_all(root);
    if (failures) return 1;
    std::puts("PASS: script mod overrides, patches and failure isolation on Lua 5.1.5");
    return 0;
}
