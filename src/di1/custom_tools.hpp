// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/character_assets.hpp"
#include "di1/tool_table.hpp"

namespace crabe::di1 {
struct CustomTool {
    ToolTableEntry table;
    std::wstring modPath;
    std::string displayName, description, sourceActor, sourceDna, sourceScript;
    // Native tool to copy; its actor, DNA, script and gun are inferred when omitted.
    std::string source;
    // Author Lua for the private tool DNA (see CustomGameplay).
    std::string overlayPatch, dnaScriptSource;
    std::string model, icon, assetDirectory, weaponSource;
    std::vector<std::string> characters, preloadActors, preloadFiles, preloadIncludes;
    std::optional<double> radius, scale, speed, lifetime, damage;
    bool equip{};
    ResourceAliases assetAliases;
    std::vector<std::uint32_t> targetSkus;
    std::string privateDna, privateScript, privateWeapon, group;
    std::uint32_t row{}, hash{};
};
// One standalone [Tool] per enabled mod. The first implementation grants it to
// named custom characters, leaving all original native inventory profiles intact.
std::vector<CustomTool> loadCustomTools(const std::vector<mods::Mod>& mods, std::vector<std::string>& messages);
void prepareCustomTools(mods::AssetIndex& assets, const std::wstring& gameAssets,
    std::vector<CustomTool>& tools, std::vector<CustomFigure>& figures, std::vector<std::string>& messages);
void addCustomToolPatches(mods::Index& index, const std::vector<CustomTool>& tools);
void setCustomToolLabels(const std::vector<CustomTool>& tools);
const char* customToolText(const char* key) noexcept;
std::string customToolDnaPatch(const CustomTool& tool);
std::string customToolActorPatch(const std::vector<CustomTool>& tools);
std::string customToolDependenciesPatch(const std::vector<CustomTool>& tools);
std::string customToolWeaponPatch(const std::vector<CustomTool>& tools);
std::string customToolIconPatch(const std::vector<CustomTool>& tools);
}
