// SPDX-License-Identifier: GPL-3.0-only
// Engine-neutral asset mods. A mod supplies files below an "assets" folder, loose in
// mods\<mod>\assets\ or inside ordinary .zip packages in mods\<mod>\. Each file either replaces a
// file the game opens from disk (videos, sound banks, whole archives) or a member of one of the
// game's archives. Archives a mod touches are rebuilt once into a cache in the game's own layout;
// the game is then pointed at the cached copy whenever it opens the original.
#pragma once
#include "mods/mod_index.hpp"
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::mods {
struct AssetRedirect {
    std::string path;     // Below assets, lowercase, '/' separated.
    std::wstring target;  // File to open instead.
};

class AssetIndex {
public:
    // gameAssets: the game's assets folder. cache: folder for rebuilt archives, created on demand
    // and reused while every input is unchanged. Problems are described in messages; a file that
    // cannot be placed is skipped without affecting the rest.
    static AssetIndex build(const std::wstring& gameAssets, const std::wstring& cache,
        const std::vector<Mod>& mods, std::vector<std::string>& messages);
    bool empty() const noexcept { return redirects.empty(); }
    // Hot path: no allocation. path as produced by assetPath().
    const std::wstring* find(std::string_view path) const noexcept;

    std::vector<AssetRedirect> redirects;  // Sorted by path.
};

constexpr std::size_t maxAssetPath = 512;
// Reduces a file name passed to Windows to its path below the last "assets" folder: lowercase,
// '/' separated. Returns 0 for anything else, for non-ASCII names and for files below "mods".
std::size_t assetPath(std::string_view fileName, char (&out)[maxAssetPath]) noexcept;
std::size_t assetPath(std::wstring_view fileName, char (&out)[maxAssetPath]) noexcept;
}
