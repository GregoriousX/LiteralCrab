// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// Camera options from LiteralCrab.ini. Hooks nothing while every option keeps the game's value.
bool installCameraHooks(HMODULE host, const Config& options);
}
