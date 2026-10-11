// SPDX-License-Identifier: GPL-3.0-only
// Toy Box packages: ZIP files with any name, anywhere below the scanned folders. A package may
// hold one Toy Box or many, from the PC or a console, in either layout the game writes:
//   - Toy Box Share (UGC) files: data, header and screenshot (U<n>, UH<n>, UC<n>);
//   - Toy Box saves: data, header and screenshot (SRR<n>, SHRR<n>, SCRR<n>).
// Member names do not matter: files are recognised by content and grouped by the number at the
// end of their name within a folder. Every Toy Box is checked (ZIP CRC, both save checksums,
// the layout and the world it needs) and converted once into PC Toy Box Share files in the
// cache; a damaged or unknown file is skipped and logged without affecting the others.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace crabe::toyboxes {
using LogFunction = void (*)(const char* format, ...);
using WorldCheck = bool (*)(const std::string& world);

// A folder of packages. Online folders hold the server mirror: their Toy Boxes are listed under
// "Disney's Toy Boxes", those of local folders under "My Toy Boxes".
struct Source {
    std::wstring folder;
    bool online{};
};

struct Toybox {
    std::string name;
    std::wstring files[3];      // Data, header, screenshot (empty when the package has none).
    std::uint32_t sizes[3]{};
    std::uint64_t signature{};  // Identifies the same Toy Box in different packages.
    bool online{};
};

// Converts what changed since the last run and returns every valid Toy Box, sources in the given
// order, packages in path order and Toy Boxes in package order. Stale cache files are removed.
std::vector<Toybox> buildLibrary(const std::vector<Source>& sources, const std::wstring& cacheDirectory,
    WorldCheck worldExists, LogFunction log, std::size_t maxToyboxes = 5000);
}
