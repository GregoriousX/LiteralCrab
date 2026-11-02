// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"

namespace crabe::di1 {
// [Gameplay] HexDiscsInPlaysets: the portal's tag handler applies a hexagonal power disc (a virtual
// reader hex coin: vehicle, mount, tool, sky or terrain) only in the Toy Box; elsewhere it shows
// IGP_HexCoinOnlyToybox and ignores the disc. The option lets such a disc take the Toy Box path in
// play sets and adventures too; what the disc then does is the game's own handling.
bool installHexDiscs(HMODULE host, const Config& options);
void removeHexDiscs() noexcept;
}
