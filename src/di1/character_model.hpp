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
#include <array>
#include <map>
#include <set>

namespace crabe::di1 {
struct ModelInfo {
    bool entity{};        // A BENT exists for the model path.
    bool scene{};         // The mesh scene exists (with or without an entity).
    bool playerSets{};    // The entity defines Core:BaseMobility.
    std::vector<std::string> groups;
    std::set<std::string> motions; // Canonical files referenced by the entity.
    std::set<std::string> controls; // Cntrl_* skeleton nodes named by the mesh scene.
};
ModelInfo inspectModel(NativeCatalog& catalog, std::string_view model);
// Animation groups the shipped entity defines in the game's own archive,
// ignoring mod providers (for example "Mech:Ship" for Davy Jones).
std::set<std::string> shippedGroups(NativeCatalog& catalog, std::string_view model);
// Bind-pose positions of the mesh's skin joints (lowercase names), scaled to
// the head's height. Empty when the scene is missing or unreadable.
std::map<std::string, std::array<double, 3>> bindPose(NativeCatalog& catalog, std::string_view model);
// Mean offset of the shared limb/spine joints, or a negative value with fewer
// than eight in common. Shared clips bind through characters/biped/biped.ct by
// channel without retargeting, so a donor's poses fit rigs near its own.
double rigDistance(const std::map<std::string, std::array<double, 3>>& a, const std::map<std::string, std::array<double, 3>>& b);
// The stock biped skeleton's limb/head joints. Mission givers and other
// humanoid NPCs on their own animation trees share it with player bipeds,
// so template animation sets drive them; cars, robots and animals lack it.
bool bipedSkeleton(const ModelInfo& model);

struct PlayerModel {
    AnimationMode mode{AnimationMode::Source}; // Source: nothing was generated.
    std::size_t files{};
    std::vector<std::string> addedGroups;
    std::size_t addedAliases{}; // Shared interaction/LCRM_ aliases added to existing groups.
    bool channelsDiffer{};      // Aliases were not added: the template's channels differ.
    std::vector<std::string> donatedGroups; // "Mech:Ship<-characters/pir_davyjones/pir_davyjones".
    std::vector<std::string> skippedDonors; // Donor groups with incompatible channels or no such group.
};
// sourceModel/templateModel are entity paths such as "characters/LR_Butch/LR_Butch".
// Auto chooses Complete for player entities missing groups and Template for
// entities without player sets or without a BENT. Complete also adds the
// template's shared interaction aliases (vehicles, mounts, ship, bike, boards,
// LCRM_ poses) that existing groups lack. An entity the author mounted in the
// figure's namespace is completed in place. Throws before publishing anything
// when a referenced file is missing or a scene is malformed.
// groupDonors maps shared interaction groups (Mech:Ship, ...) to the stock
// entity path supplying them instead of the template.
PlayerModel preparePlayerModel(NativeCatalog& catalog, mods::AssetIndex& assets, std::string_view namespaceKey,
    std::string_view sourceModel, std::string_view templateModel, AnimationMode mode, ResourceAliases& aliases,
    const std::vector<std::pair<std::string, std::string>>& groupDonors = {});
}
