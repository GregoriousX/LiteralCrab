// SPDX-License-Identifier: GPL-3.0-only
// Engine-neutral asset mods. A mod supplies files below an "assets" folder, loose in
// mods\<mod>\assets\ or inside ordinary .zip packages in mods\<mod>\. Added and replaced files
// are available through native opens and the engine's member loader. Packaged files that must
// exist on disk are extracted into the mod cache; original game archives are never rebuilt.
#pragma once
#include "mods/mod_index.hpp"
#include "mods/zip.hpp"
#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::mods {
struct AssetRedirect {
    std::string path;     // Below assets, lowercase, '/' separated.
    std::wstring target;  // File to open instead.
};

// A file served to the engine's loader under the name it requests.
struct AssetMember {
    std::string name;  // Request name: lowercase, '/' separated.
    std::wstring file;  // Loose source, or empty when packaged.
    std::shared_ptr<const zip::Archive> package;
    std::size_t entry{};
    std::uint16_t mod{};
    // Host-generated resources (for example a portrait bundle referring to a
    // texture already shipped with the game). Immutable for loader threads.
    std::shared_ptr<const std::string> generated;
};

class AssetIndex {
public:
    // gameAssets: the game's assets folder. cache: folder for files that must exist on disk
    // but come from a package (extracted during indexing). Problems are described in messages.
    static AssetIndex build(const std::wstring& gameAssets, const std::wstring& cache,
        const std::vector<Mod>& mods, std::vector<std::string>& messages);
    bool empty() const noexcept { return redirects.empty() && members.empty(); }
    // Hot paths: no allocation. path as produced by assetPath(); name as the engine requests it.
    const std::wstring* find(std::string_view path) const noexcept;
    // True when a winning disk redirect exists below this directory. Normalized
    // path below assets, without a trailing slash; no disk writes or allocation.
    bool hasDirectory(std::string_view path) const noexcept;
    const AssetMember* member(std::string_view name) const noexcept;
    // Reads a member's data; thread-safe. False when the file vanished or is damaged.
    bool read(const AssetMember& member, std::string& data) const noexcept;

    std::vector<AssetRedirect> redirects;  // Sorted by path.
    std::vector<AssetMember> members;      // Sorted by name.
};

constexpr std::size_t maxAssetPath = 512;
// Reduces a file name passed to Windows to its path below the last "assets" folder: lowercase,
// '/' separated. Returns 0 for anything else, for non-ASCII names and for files below "mods".
std::size_t assetPath(std::string_view fileName, char (&out)[maxAssetPath]) noexcept;
std::size_t assetPath(std::wstring_view fileName, char (&out)[maxAssetPath]) noexcept;
// Lowercases and converts '\' to '/'; 0 when empty, non-ASCII or too long.
std::size_t requestName(std::string_view name, char (&out)[maxAssetPath]) noexcept;
// The engine's request name for a mod file at relative (below assets): the path itself, except
// that textures are addressed globally by their content hash as "textures/<file>".
std::string memberName(std::string_view relative);
}
