// SPDX-License-Identifier: GPL-3.0-only
// Engine-neutral Lua 5.1 script mod runtime. An engine profile resolves the C API of the game's
// embedded Lua and routes its luaL_loadbuffer through loadBuffer(); everything game-specific
// (gameplay changes, chunk names) lives in external mods, never here.
#pragma once
#include "mods/mod_index.hpp"
#include <cstddef>

struct lua_State;

namespace crabe::mods {
using LuaCFunction = int(__cdecl*)(lua_State*);
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
};
using LogFunction = void (*)(const char* format, ...);

// Call once, before the hook routing to loadBuffer() is enabled. The index is never modified
// afterwards, which keeps the hook lock-free.
void configure(const LuaApi& api, Index index, LogFunction log, bool trace);
// Drop-in luaL_loadbuffer replacement: applies overrides before and patches after compilation.
int __cdecl loadBuffer(lua_State* state, const char* data, std::size_t size, const char* name);
}
