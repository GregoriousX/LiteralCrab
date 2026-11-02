// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace crabe::di1 {
// Toy Store Infinity: the Cars play set sets UseCarsTurboLocks, under which a vehicle's turbo meter
// (CarEnergy) follows the Cars turbo purchases and is only created for a vehicle that is itself a
// player's avatar. A Toy Box or other world's vehicle driven by a character therefore had no meter.
// Such a vehicle (a driver that is not the vehicle, the vehicle no player's avatar) keeps the Toy Box
// rules instead: its own MaxCarEnergy and meter. Cars avatars keep the purchase locks.
bool installToyStoreTurbo(std::uintptr_t base);
void removeToyStoreTurbo() noexcept;
}
