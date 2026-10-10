// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace crabe::di1 {
// Loads mods\<folder>\mod.ini script mods next to the ASI and hooks the game's Lua loader.
// Failure leaves game scripts and display features untouched.
bool installMods(HMODULE host);
}
