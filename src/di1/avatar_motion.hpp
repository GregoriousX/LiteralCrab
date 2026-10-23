// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include "mods/lua_mods.hpp"
namespace crabe::di1 {
// Exposes the verified named motion override on actual local-player avatars.
// A missing profile leaves the other adapters unchanged.
bool addAvatarMotionAdapters(HMODULE host, mods::LuaApi& api);
}
