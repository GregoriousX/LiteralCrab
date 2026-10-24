// SPDX-License-Identifier: GPL-3.0-only
// Startup lookups in the effective game data: a mod's asset member when one
// replaces a file, otherwise the shipped archive. Everything is read lazily,
// cached for the startup pass and never written back.
#pragma once
#include "di1/lua_data.hpp"
#include "di1/tool_table.hpp"
#include "mods/asset_mods.hpp"
#include <map>
#include <memory>
#include <optional>

namespace crabe::di1 {
struct NativeActor {
    std::string name, type, dna, parms, list; // dna: the DNAFile parameter, as written.
};
struct ArchiveEntry {
    std::shared_ptr<const mods::zip::Archive> archive;
    std::size_t entry{};
};

class NativeCatalog {
public:
    NativeCatalog(const mods::AssetIndex& assets, std::wstring gameAssets);
    // Effective bytes of a resource name such as "characters/TS_Zurg.dnax".
    std::string read(std::string_view name);
    std::optional<std::string> tryRead(std::string_view name);
    // Evaluated pure-data chunk (cached by lowercase name).
    const data::Globals& chunk(std::string_view name);
    // First actor row with this name (ignoring case), searching the playset
    // actor lists in a fixed order: Toy Box, common, Toy Story, then others.
    const NativeActor* actor(std::string_view name);
    // First data-map dependency row for an actor, searching core first.
    const data::Table* dependencies(std::string_view actor);
    ToolTableInfo tools();
    const data::Globals& weapons();
    // An archive below assets, e.g. "characters/lr_butch/lr_butch.zip"; null when absent.
    std::shared_ptr<const mods::zip::Archive> archive(std::string_view path);
    // Canonical resource names of an archive's members, as the loader requests
    // them: names below "characters/" or "textures/" are absolute, others
    // belong to the archive's folder.
    std::map<std::string, ArchiveEntry> members(std::string_view archivePath);
    const mods::AssetIndex& assets() const noexcept { return index; }
private:
    const mods::AssetIndex& index;
    std::wstring root;
    std::map<std::string, std::shared_ptr<const mods::zip::Archive>> archives;
    std::map<std::string, data::Globals> chunks;
    std::optional<std::vector<NativeActor>> actorRows;
};

std::string lowerPath(std::string_view value);
// "characters/lr_butch" + "../biped/a.oct" -> "characters/biped/a.oct"; throws on escape.
std::string joinPath(std::string_view directory, std::string_view relative);
// Relative reference from directory to a canonical path.
std::string relativePath(std::string_view directory, std::string_view target);
}
