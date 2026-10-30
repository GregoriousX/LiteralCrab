// SPDX-License-Identifier: GPL-3.0-only
// Startup data for Toy Store Infinity, read from the effective game files (mod replacements,
// loose files, then the shipped archives). A play set loads only its own and the Core packages,
// so the other worlds' vehicles, their placeable-object rows and their names come from files.
#pragma once
#include "di1/native_catalog.hpp"
#include <map>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::di1 {
struct ToyStoreToy {
    std::string name, model; // Inventory name (INV_TB_AutopiaCar) and placeable object (TB_AutopiaCar).
    std::string card;         // A play set toy's recipe card; empty for Toy Box toys.
    double sortOrder{};
    bool toyBox{};            // Toy Box toys are all unlocked in Gold.
};
struct ToyStoreLanguage {
    std::string dictionary;                    // e.g. "english_ntsc"
    std::map<std::string, std::string> texts;  // lowercase key -> text, world dictionaries
    std::map<std::string, std::string> probes; // toyStoreProbeKeys -> text, main dictionary
};
struct ToyStoreData {
    std::vector<ToyStoreToy> toys;
    // Lua list of object rows for play set packages: the session-only rows the store places and
    // the original rows Core lacks (saves made by earlier builds may name them).
    std::string objects;
    std::vector<ToyStoreLanguage> languages;
    std::set<std::string> keys; // Every lowercase name/description key of the toys.
    // A play set toy is shown under its recipe card; when a card has no text, its inventory
    // item's (rc_x_name -> inv_x_name).
    std::map<std::string, std::string> aliases;
    // Dependency files of the toys' actors (behavior, animation trees, scripts, choreography)
    // that a play set loads only for its own actors; models are left to stream on placement.
    std::vector<std::string> dependencies;
};

// Texts of the given lowercase keys from a native DICT pair: stringids (key per hash) and a
// language file (text per hash). Throws std::runtime_error on a malformed file or when the
// two files are not of the same dictionary build.
std::map<std::string, std::string> readDictionaryTexts(std::string_view stringIds, std::string_view language,
    const std::set<std::string>& keys);
// The vehicles and mounts of the Toy Box and of every play set that can be placed in a play set.
// Missing or unreadable optional parts (a play set's data, a dictionary) only leave them out;
// messages say what was used.
ToyStoreData readToyStoreData(NativeCatalog& catalog, std::vector<std::string>& messages);
// Main-dictionary keys (pause menu texts) whose native texts tell which language the game uses.
inline constexpr std::string_view toyStoreProbeKeys[] = {"scn_pause_options", "scn_pause_resume", "sharedtext_quit",
    "sharedtext_save", "sharedtext_load", "frontend_pause"};
// RumpusData package names of the play sets that receive other worlds' object rows. Pirates has no
// object data of its own; Toy Store Infinity gives it an empty package (toyStorePiratesObjects).
inline constexpr std::string_view toyStorePackages[] = {"Cars", "Incredibles", "LoneRanger", "Monsters", "ToyStoryInSpace", "Pirates"};
// The store places private copies of the toys' object rows, flagged M_NoSave like the game's own
// Buzz ride and trains, so its toys never enter a play set save; the originals stay untouched.
inline std::string toyStorePlacedName(std::string_view model) { return "LiteralCrab_TSI_" + std::string(model); }
inline constexpr std::string_view toyStorePiratesObjectsPath = "gamedb/pirates/rumpusdata.lua";
inline constexpr std::string_view toyStorePiratesObjects =
    "Data = {DBName = \"RumpusData\", PackageName = \"Pirates\", Records = {}, Type = \"RumpusObjects\", Version = 0.3}\n";
}
