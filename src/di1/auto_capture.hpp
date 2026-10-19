// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace crabe::di1 {
// An in-process thread writes dump/log ZIPs. No helper executable is launched.
// Arm during initialization; start only from the post-loader graphics callback.
void armAutomaticCapture(HMODULE plugin, const wchar_t* directory);
void startAutomaticCapture() noexcept;
bool stopAutomaticCapture() noexcept;
}
