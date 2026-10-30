// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace crabe::di1 {
// Toy Store Infinity: Pirates' sea has a water collision surface every vehicle drives on. A character
// who enters the water meets a filtered walk-in trigger below the surface (Water_Respawn, Water_Respot,
// Death_Trigger_Main) wired to Respawn_Event_WATER; vehicles never pass its actor filter and ride above
// it. A driven Toy Box vehicle (ToyBoxVehicle component, not a ship) low on the water over such a
// trigger's area, or found by the trigger itself, is killed as other worlds' water triggers do (Meridian
// RespawnOrDie: CActor::Kill), and its native TardisRespawn sets it back on land. The triggers' own
// filtering is unchanged; levels without an ocean are untouched.
bool installToyStoreWater(std::uintptr_t base);
void removeToyStoreWater() noexcept;
}
