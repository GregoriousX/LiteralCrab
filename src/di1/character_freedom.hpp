// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"
#include <string_view>

namespace crabe::di1 {
// [Gameplay] CharactersInAnyPlayset: the native playset checks, patched in memory. Changes
// nothing unless the option is on; all sites are verified first and patched together or not at all.
bool installCharacterFreedom(HMODULE host, const Config& options);
// True once the native half is in place; the zone list patch is only registered then.
bool characterFreedomActive() noexcept;

// The data half: the zone list patch that lets each play set accept every character. Registered
// with the script mod loader by installMods.
extern const std::string_view playableAvatarsPatch;
}
