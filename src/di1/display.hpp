// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
bool installDisplayHooks(HMODULE host, const Config& options);
void removeDisplayHooks();
struct DisplaySnapshot { HWND window; unsigned width, height; Mode mode; bool managed; std::uint64_t presents; };
DisplaySnapshot displaySnapshot();
// Diagnostics: called on the window thread after the game handled WM_ACTIVATE.
void setFocusObserver(void (*observer)(bool active));
}
