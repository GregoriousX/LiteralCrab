// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <vector>

namespace crabe::di1 {
// Validated engine capabilities for script mods (LiteralCrab.<name> in patches). Each group is
// verified on its own; a group whose code differs is left out and logged, the rest stay.
// api must already hold the Lua functions adapters use (toNumber, toLString, pushBoolean...).
std::vector<mods::LibraryFunction> resolveAdapters(HMODULE host, const mods::LuaApi& api);
}
