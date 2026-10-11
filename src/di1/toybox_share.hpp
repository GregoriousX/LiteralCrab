// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"
#include <string_view>

namespace crabe::di1 {
// [ToyBoxes] Enabled: the Toy Box Share menu with the Toy Boxes of the toyboxes folder and servers.
bool installToyBoxShare(HMODULE host, const Config& options);
bool toyBoxShareActive() noexcept;
// Script half: adds the menu's entry to the Toy Box menu (Presentation/ToyboxSelectMenu.lua).
extern const std::string_view toyBoxShareMenuPatch;
}
