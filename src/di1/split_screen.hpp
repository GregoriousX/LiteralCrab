// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// [SplitScreen] Enabled: a second controller joins as local player 2. Hooks nothing while off.
bool installSplitScreen(HMODULE host, const Config& options);
}
