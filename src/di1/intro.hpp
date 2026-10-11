// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// [Startup] SkipIntro: the startup logo movies are not played. Game files stay untouched;
// nothing changes while the option is off.
bool installIntroSkip(HMODULE host, const Config& options);
}
