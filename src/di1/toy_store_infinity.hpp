// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/toy_store_data.hpp"
#include <string>
#include <string_view>

namespace crabe::di1 {
// [ToyStore] Infinity: a second play set Toy Store, "Toy Store Infinity", built on the native
// Catalog screen. It lists the play set's unlocked vehicles and mounts and the Toy Box's
// (unlocked in Gold) and places the chosen one beside the player. [ToyStore] InfinityAll=1
// adds the play set's locked ones. Nothing is bought, counted or saved: placing never changes
// inventory or recipe cards.
inline constexpr std::string_view toyStoreInfinityKey = "LiteralCrab_ToyStoreInfinity";
// The entry's and screen's title for the localized-label hook; nullptr for other keys.
const char* toyStoreInfinityText(const char* key) noexcept;
// Startup data; the Toy Box toy names below come from it.
void setToyStoreData(ToyStoreData data);
// Native text of a key in the game's current dictionary, nullptr when it has none.
using NativeText = const char* (*)(void* context, const char* key);
// A store toy's name or description the current world's dictionaries lack, in the game's
// language (recognised once from the native texts of toyStoreProbeKeys); nullptr otherwise.
const char* toyStoreToyText(const char* key, NativeText native, void* context);
// Options of the Catalog patch: {All=..., Toys={{name=, model=, card=, sort=, toyBox=}, ...}}.
std::string toyStoreCatalogOptions(bool all);
// Options of the play set object-row patch: {Packages={...}, Objects={...}}; empty without rows.
std::string toyStoreObjectOptions();
// Options of the dependency patch: {Files={...}}; empty without files.
std::string toyStoreDependencyOptions();
// Presentation/PauseMenu.lua and Presentation/SelectMenu.lua (the play set's Infinity kiosk):
// the entry, shown only in an ordinary play set menu, opens the Catalog in Infinity mode.
extern const std::string_view toyStoreInfinityMenuPatch;
// Presentation/Catalog.lua: the Infinity mode.
extern const std::string_view toyStoreInfinityCatalogPatch;
// GameDB/<play set>/rumpusdata.lua: adds the object rows other worlds' vehicles need.
extern const std::string_view toyStoreInfinityObjectsPatch;
// GameDB/Core/CoreDataMap.lua: loads those vehicles' and mounts' dependencies in every play set.
extern const std::string_view toyStoreInfinityDependenciesPatch;
// Characters/Avatar.btx: the avatar perceives a bike's ride interaction in every play set, not only Monsters U.
extern const std::string_view toyStoreInfinityRidingPatch;
// GameDB/Core/PiratesDataMap.lua: loads the generated Pirates object package beside its recipe cards.
extern const std::string_view toyStoreInfinityPiratesPatch;
}
