// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/figure_registry.hpp"
#include "mods/asset_mods.hpp"
#include <map>

namespace crabe::di1 {
using ResourceAliases = std::vector<std::pair<std::string, std::string>>;
// Mounts one mod-local folder into a canonical, caller-owned namespace. Keys
// are lowercase resource paths, for example "1000900" or "tools/lcr_blaster".
// Validation failures throw without publishing members, redirects or aliases.
void preparePrivateAssetDirectory(mods::AssetIndex& assets, const std::wstring& modPath,
    std::string_view directory, std::string_view namespaceKey, ResourceAliases& aliases);
std::string privateAssetPath(const ResourceAliases& aliases, std::string_view logical);
// Rewrites the 8-byte texture keys of a native BNDL v1 / TEXB v3 bundle and
// validates its MATP metadata; keys absent from the map are kept.
void rebindBundleTextures(std::string& bundle, const std::map<std::string, std::string>& textures);
std::string privateAssetDnaPatch(const ResourceAliases& aliases);
// Mounts only the declaring mod's AssetDirectory under a SKU-specific namespace.
// Failed mounts publish nothing and remove the figure before portal registration.
void prepareCustomAssetDirectories(mods::AssetIndex& assets, std::vector<CustomFigure>& figures,
    std::vector<std::string>& messages);
std::string customAssetPath(const CustomFigure& figure, std::string_view logical);
// Lua code remaps supported resource strings only in a private DNA tree.
std::string customAssetDnaPatch(const CustomFigure& figure);
void addCustomAssetPatches(mods::Index& index, const std::vector<CustomFigure>& figures);
}
