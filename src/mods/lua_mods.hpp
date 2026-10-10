// SPDX-License-Identifier: GPL-3.0-only
// Engine-neutral Lua 5.1 script mod runtime. An engine profile resolves the C API of the game's
// embedded Lua and routes its luaL_loadbuffer through loadBuffer(); everything game-specific
// (gameplay changes, chunk names) lives in external mods, never here.
#pragma once
#include "mods/mod_index.hpp"
#include <cstddef>
#include <string>
#include <vector>

struct lua_State;

namespace crabe::mods {
using LuaCFunction = int(__cdecl*)(lua_State*);
// A C function of the engine's Lua standard library, named as Lua sees it: "pcall",
// "table.insert", "string.format", "math.floor".
struct LibraryFunction {
    std::string name;
    LuaCFunction function{};
};
struct LuaApi {
    // Must reach the original, unhooked luaL_loadbuffer.
    int(__cdecl* loadBuffer)(lua_State*, const char*, std::size_t, const char*){};
    int(__cdecl* getTop)(lua_State*){};
    void(__cdecl* setTop)(lua_State*, int){};
    void(__cdecl* pushValue)(lua_State*, int){};
    void(__cdecl* insert)(lua_State*, int){};
    void(__cdecl* pushClosure)(lua_State*, LuaCFunction, int){};
    int(__cdecl* pcall)(lua_State*, int, int, int){};
    const char*(__cdecl* toLString)(lua_State*, int, std::size_t*){};
    // The engine's base, table, string and math library functions. Patches receive the ones on
    // the runtime's allowlist even in Lua states the game opened without any library.
    std::vector<LibraryFunction> library;
    // Validated engine capabilities, visible to patches as LiteralCrab.<name>; patches test them
    // with LiteralCrab.has(name). Supplied by the engine profile, empty when none verified.
    std::vector<LibraryFunction> adapters;
    // Used by adapters to read arguments and return results.
    double(__cdecl* toNumber)(lua_State*, int){};
    int(__cdecl* toBoolean)(lua_State*, int){};
    void(__cdecl* pushBoolean)(lua_State*, int){};
    int(__cdecl* type)(lua_State*, int){};
};
using LogFunction = void (*)(const char* format, ...);

// Call once, before the hook routing to loadBuffer() is enabled. The index is never modified
// afterwards, which keeps the hook lock-free. Fails if the library lacks a function the patch
// wrapper needs.
bool configure(const LuaApi& api, Index index, LogFunction log, bool trace);
// Drop-in luaL_loadbuffer replacement: applies overrides before and patches after compilation.
int __cdecl loadBuffer(lua_State* state, const char* data, std::size_t size, const char* name);
}
