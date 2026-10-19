// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <vector>

namespace crabe::di1 {
// A hook installed by this ASI after validating the native query. The resolver
// independently checks its detour, copied prologue and return continuation.
struct ToolQueryHook {
    const void* trampoline{};
    const void* detour{};
};
// Validated engine capabilities for script mods (LiteralCrab.<name> in patches). Each group is
// verified on its own; a group whose code differs is left out and logged, the rest stay.
// api must already hold the Lua functions adapters use (toNumber, toLString, pushBoolean...).
std::vector<mods::LibraryFunction> resolveAdapters(HMODULE host, const mods::LuaApi& api, ToolQueryHook toolQuery = {});
}
