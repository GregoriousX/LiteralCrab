// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>

namespace crabe::di1 {
// Engine feature "TriggerGuiScreen" ([Engine] TriggerGuiScreen=1 in a mod.ini): world graph
// TriggerGuiScreen nodes run their ScreenCommand as in the console builds. Hooks nothing unless
// a mod asks for it; false (and logged) when the game's code differs.
bool installGuiScreenCommands(HMODULE host);
}
