// SPDX-License-Identifier: GPL-3.0-only
// Engine-neutral script mod index: discovers mods\<folder>\mod.ini once at startup and maps
// game Lua chunk names to override sources and patch scripts. Immutable after loading, so the
// chunk-load hook reads it from any thread without locking or allocation.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::mods {
// Longest chunk identity considered for matching; longer names never match.
constexpr std::size_t maxChunkName = 260;
// Distinct configured paths that may name one chunk ("adventures.lua", "presentation/adventures.lua").
constexpr std::size_t maxMatches = 8;

struct Script {
    std::string chunk;   // "@mods/<folder>/<file>" so Lua errors name the mod file.
    std::string source;  // Patch scripts: the per-patch builder with label and options baked in.
    std::string body;    // Patch scripts only: the mod's Lua source.
    std::uint16_t mod{};
};

struct Target {
    std::string path;                    // Normalized: lowercase, '/' separators, no '@'.
    std::size_t basename{};              // Offset of the component after the last '/'.
    int override = -1;                   // Index into Index::scripts, -1 = game chunk kept.
    std::vector<std::uint16_t> patches;  // Indices into Index::scripts, in mod load order.
    std::string_view name() const noexcept { return std::string_view(path).substr(basename); }
};

struct Mod {
    std::string folder;
    std::string name;
};

class Index {
public:
    // Reads every enabled mods\<folder>\mod.ini (folders in ordinal order). Invalid mods are
    // skipped as a whole and described in messages; valid ones are unaffected.
    static Index load(const std::wstring& modsRoot, std::vector<std::string>& messages);
    bool empty() const noexcept { return targets.empty(); }
    // Hot path: no allocation. identity is a chunk name as the engine reports it. Writes every
    // target whose path is a '/'-bounded suffix of it, longest path first; returns the count.
    std::size_t find(std::string_view identity, const Target* (&out)[maxMatches]) const noexcept;

    std::vector<Mod> mods;
    std::vector<Script> scripts;
    std::vector<Target> targets;  // Sorted by basename, then longest path first.
};

// The name a chunk is known by: the source name embedded in a Lua 5.1 binary chunk header when
// present (the game ships precompiled scripts), otherwise the name passed to the loader.
std::string_view chunkIdentity(const char* data, std::size_t size, const char* name) noexcept;
// Lowercases, converts '\' to '/', strips a leading '@' or '=' and "./". Returns the length
// written, or 0 if the name is empty or does not fit.
std::size_t normalize(std::string_view name, char (&out)[maxChunkName]) noexcept;
// Lua string literal with all non-printable bytes escaped.
std::string luaQuote(std::string_view text);
}
