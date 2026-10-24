// SPDX-License-Identifier: GPL-3.0-only
// Automatic preparation of custom characters and tools from native data: the
// source DNA and its actor row, the player template, inventory, starting
// equipment, NPC tool controls, weapon clones and the private player model.
// Explicit manifest values always win; anything ambiguous is an error that
// names the setting to add.
#pragma once
#include "di1/character_assets.hpp"
#include "di1/native_catalog.hpp"

namespace crabe::di1 {
// Fills the resolved fields of CustomGameplay. Throws with an actionable reason.
void resolveCustomCharacter(NativeCatalog& catalog, mods::AssetIndex& assets, CustomFigure& figure,
    std::vector<std::string>& messages);

std::vector<std::string> slngLiterals(std::string_view script);
struct ToolSource { std::string actor, dna, script, weapon; };
// The spawned actor, its DNA and native script of a tool row with exactly one
// actor descriptor; weapon is the unique native gun its script names, when
// wantWeapon is set. Paths go through the given private aliases.
ToolSource inferToolSource(NativeCatalog& catalog, const ToolRowInfo& tool, const ResourceAliases& aliases, bool wantWeapon);
// Preloads for a gun's projectile visuals that exist in the shipped data.
std::vector<std::string> weaponEffects(NativeCatalog& catalog, std::string_view gun);
// Default player control template for an NPC-only Items row.
constexpr const char* defaultItemControl = "ITEM_BLASTER";
}
