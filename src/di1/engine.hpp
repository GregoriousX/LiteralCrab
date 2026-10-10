// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace crabe::di1 {
bool supportedBuild(HMODULE module) noexcept;
bool installEngineHooks(HMODULE module);
void removeEngineHooks();
}
