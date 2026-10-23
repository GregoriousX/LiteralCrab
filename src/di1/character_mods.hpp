// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/figure_registry.hpp"
#include "mods/asset_mods.hpp"
#include "mods/lua_mods.hpp"

namespace crabe::di1 {
// Startup-only registration; every definition comes from an enabled mod.
void addCustomCharacterPatches(mods::Index& index, const std::vector<CustomFigure>& figures);
// Fits original/mod-supplied icons onto private portal canvases and bundles.
// Returns only definitions whose portrait prerequisites could be resolved.
void prepareCustomCharacterAssets(mods::AssetIndex& assets, const std::wstring& gameAssets,
    std::vector<CustomFigure>& figures, std::vector<std::string>& messages);
void setCustomCharacterLabels(const std::vector<CustomFigure>& figures);
void addCustomCharacterAdapters(mods::LuaApi& api);
const char* customCharacterText(const char* key) noexcept;
std::string customCharacterCatalogPatch(const std::vector<CustomFigure>& figures);
std::string customCharacterPortalPatch(const std::vector<CustomFigure>& figures);
}
