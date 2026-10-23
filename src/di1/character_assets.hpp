// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/figure_registry.hpp"
#include "mods/asset_mods.hpp"

namespace crabe::di1 {
// Mounts only the declaring mod's AssetDirectory under a SKU-specific namespace.
// Failed mounts publish nothing and remove the figure before portal registration.
void prepareCustomAssetDirectories(mods::AssetIndex& assets, std::vector<CustomFigure>& figures,
    std::vector<std::string>& messages);
std::string customAssetPath(const CustomFigure& figure, std::string_view logical);
// Lua code remaps supported resource strings only in a private DNA tree.
std::string customAssetDnaPatch(const CustomFigure& figure);
void addCustomAssetPatches(mods::Index& index, const std::vector<CustomFigure>& figures);
}
