// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"
#include "mods/lua_mods.hpp"
#include "di1/character_freedom_data.hpp"

namespace crabe::di1 {
struct ToolQueryHook;
// [Gameplay] CharactersInAnyPlayset: the native playset checks, patched in memory. Changes
// nothing unless the option is on; all sites are verified first and patched together or not at all.
bool installCharacterFreedom(HMODULE host, const Config& options);
// True once the native half is in place; the ownership/FX patch is only registered then.
bool characterFreedomActive() noexcept;
ToolQueryHook characterToolQueryHook() noexcept;
// Reuse the owned initialization trace hook when it is active.
bool beforeCharacterToolInitialization(void (*callback)(void*) noexcept) noexcept;
// Undo this feature if the script loader cannot install its data half.
void removeCharacterFreedom() noexcept;
void addCharacterFreedomAdapters(mods::LuaApi& api);

}
