// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// [PostProcessing] options from LiteralCrab.ini. Hooks nothing while every effect stays on.
bool installPostEffectHooks(HMODULE host, const Config& options);
}
