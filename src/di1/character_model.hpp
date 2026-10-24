// SPDX-License-Identifier: GPL-3.0-only
// Private animated entities for custom characters. The avatar animation tree
// plays named animation sets (Core:BaseMobility, Mech:Hoverboard, ...) that
// player models define in their BENT entity. NPC models lack most of them and
// some cut characters ship only a mesh. This module builds a private entity
// that keeps the source mesh and borrows a stock template's animation sets,
// serving every file from the game's own archives under the figure's private
// namespace. Stock entities are never replaced.
#pragma once
#include "di1/character_assets.hpp"
#include "di1/native_catalog.hpp"
#include <set>

namespace crabe::di1 {
struct ModelInfo {
    bool entity{};        // A BENT exists for the model path.
    bool scene{};         // The mesh scene exists (with or without an entity).
    bool playerSets{};    // The entity defines Core:BaseMobility.
    std::vector<std::string> groups;
    std::set<std::string> motions; // Canonical files referenced by the entity.
};
ModelInfo inspectModel(NativeCatalog& catalog, std::string_view model);

struct PlayerModel {
    AnimationMode mode{AnimationMode::Source}; // Source: nothing was generated.
    std::size_t files{};
    std::vector<std::string> addedGroups;
};
// sourceModel/templateModel are entity paths such as "characters/LR_Butch/LR_Butch".
// Auto chooses Complete for player entities missing groups and Template for
// entities without player sets or without a BENT. Throws before publishing
// anything when a referenced file is missing or a scene is malformed.
PlayerModel preparePlayerModel(NativeCatalog& catalog, mods::AssetIndex& assets, std::string_view namespaceKey,
    std::string_view sourceModel, std::string_view templateModel, AnimationMode mode, ResourceAliases& aliases);
}
