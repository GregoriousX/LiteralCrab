// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "mods/mod_index.hpp"
#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::di1 {
struct CustomItemWeapon {
    std::string source, dna, script, sourceActor;
    std::optional<double> radius, speed, lifetime, damage, scale;
    std::string actor, privateDna, privateScript, privateWeapon;
    bool enabled() const noexcept { return !source.empty(); }
};
struct CustomGameplay {
    std::string sourceActor, sourceDna, item, prop, ability;
    std::vector<std::string> preloadActors, preloadFiles;
    std::array<std::string, 2> controlSources;
    // Resolved from the effective table at startup; not manifest row numbers.
    std::array<std::uint32_t, 2> controlRows{~0u, ~0u}, controlHashes{};
    std::array<std::string, 3> toolDisplayNames;
    CustomItemWeapon itemWeapon;
    std::optional<std::array<double,3>> aimOffset;
    std::string inventorySource;
    std::vector<std::string> inventoryExclude;
    // Ordered native tool indices, ending in ~0u, plus the expected row IDs.
    // Only used for marked private avatars; source profiles remain unchanged.
    std::vector<std::uint32_t> inventoryRows, inventoryHashes;
    std::vector<std::string> preloadIncludes;
    // Private native Avatar tree with zero-piece death event ordering repaired.
    std::string respawnBehavior;
    bool enabled() const noexcept { return !sourceDna.empty(); }
};
struct CustomFigure {
    std::uint32_t sku{};
    std::string name, gateway, actor, baseCharacter, displayName, description, icon, playset;
    CustomGameplay gameplay;
    // Opt-in assets belong to this enabled mod, never a global replacement.
    std::string assetDirectory;
    std::wstring modPath;
    std::vector<std::pair<std::string, std::string>> assetAliases;
};

// The input is the already enabled, ordered mod list. Reads only [Character] in mod.ini;
// user.ini contributes only through the mod index's existing enable/disable decision.
std::vector<CustomFigure> loadCustomFigures(const std::vector<mods::Mod>& mods,
    std::vector<std::string>& messages);
// Appends encrypted DI1 Category1 rows after the original chunk runs. Unknown table shapes,
// full tables and conflicting rows are left unchanged; repeating the patch is idempotent.
std::string customFigureGatewayPatch(const std::vector<CustomFigure>& figures);
// Raw bytes identifying the Category1 encrypted key in the original compiled gateway chunk.
std::string_view customFigureGatewayMarker();

// Pure startup helpers, also exposed for byte-exact format regression tests. DI1 field IVs
// depend on the one-based array position; they differ from the original CrabeLoader format.
std::string encryptFigureString(std::string_view value, std::uint32_t row);
std::string encryptFigureNumber(double value, std::uint32_t row);
}
