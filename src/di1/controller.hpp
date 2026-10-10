// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// [Controller] options from LiteralCrab.ini. Hooks nothing while every option keeps the game's
// own button layout.
bool installControllerHooks(HMODULE host, const Config& options);
}
