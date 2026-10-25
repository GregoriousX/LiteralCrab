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
// How the private player DNA is built. Auto uses the source DNA when it is
// already a player avatar, otherwise the template's player DNA with the
// source's visual identity grafted onto it.
enum class BodyMode { Auto, Source, Template };
// How the private model gets the player animation sets the avatar tree uses.
// Complete appends the template's missing groups to the source model; Template
// binds the source mesh to the template's whole animation entity.
enum class AnimationMode { Auto, Source, Complete, Template };
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
    // Standalone [Tool] declarations grant only these private avatar entries.
    // Resolved at startup; never parsed from Character.Gameplay row numbers.
    std::vector<std::string> extraTools;
    std::vector<std::string> preloadIncludes;
    // Private native Avatar tree with zero-piece death event ordering repaired.
    std::string respawnBehavior;
    // Stock playable figure supplying the player body, animations and basic
    // inventory when the source lacks them. Empty: BaseCharacter, then automatic.
    std::string templateFigure;
    BodyMode body{BodyMode::Auto};
    AnimationMode animations{AnimationMode::Auto};
    // Stock figure whose animation entity drives the player (default: the
    // template), independent of the body/inventory template.
    std::string animationTemplate;
    // Per shared interaction group (Mech:Ship, ...) the stock figure supplying
    // its animations; unset groups use the shipped rig nearest the character's.
    std::vector<std::pair<std::string, std::string>> animationDonors;
    // Author Lua: a data-only DNA table merged into the private DNA, and an
    // optional script run on it afterwards (mod-relative paths).
    std::string dnaOverlay, dnaScript, dnaScriptSource;
    // Resolved at startup from native data (never manifest row numbers).
    std::string sourceMask;              // Source ActorTypeMask, the native profile key.
    std::string expectedMask;            // ActorTypeMask the private DNA must carry.
    std::string rowSource, rowSourceDna; // Native actor/dependency rows cloned for the private actor.
    std::string bodyDna;                 // DNA whose bytes become the private DNA.
    std::string bodyPatch;               // Lua grafting source identity / model fixes.
    std::string overlayPatch;            // Lua applying the validated author overlay.
    std::vector<std::pair<std::string, std::string>> fileReplacements; // "kind|path" (lower) -> file, "" removes.
    bool stripModelDef{};                // Template rows' modelDef would select the template's model.
    bool exactLoadout{};                 // AutoStartTool1..3 become exactly item/prop/ability.
    bool enabled() const noexcept { return !sourceDna.empty() || !sourceActor.empty(); }
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
// Stable default SkuId for a Name: 1000100..1999999.
std::uint32_t defaultFigureSku(std::string_view name);
// Stock playable bipeds usable as body/animation/inventory templates.
bool templateFigure(std::string_view gateway);
const std::vector<std::string_view>& templateFigures();
// "Ship" or "Mech:Ship" -> "Mech:Ship" for shared interaction groups; empty otherwise.
std::string interactionGroupName(std::string_view name);
// Validated Lua overlay (global DNA table and optional Remove list) as patch code.
std::string dnaOverlayPatch(std::string_view source, std::string_view label);
}
