// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/figure_registry.hpp"
#include "mods/asset_mods.hpp"

namespace crabe::di1 {
// Startup preparation reads the effective mod/original tool table and DNA.
// Invalid gameplay definitions are removed before any portal registration.
void prepareCustomCharacterGameplay(mods::AssetIndex& assets, const std::wstring& gameAssets,
    std::vector<CustomFigure>& figures, std::vector<std::string>& messages);
void addCustomGameplayPatches(mods::Index& index, const std::vector<CustomFigure>& figures);
std::string customGameplayDnaPath(const CustomFigure& figure);
// Chunk targets of the private DNA: its path, embedded identity and content
// marker. "contains:" patches never see it, because it is a named chunk.
std::vector<std::string> customGameplayDnaTargets(const CustomFigure& figure);
std::string customGameplayDnaPatch(const CustomFigure& figure);
std::string customRespawnBehaviorPatch();
std::string customGameplayActorPatch(const std::vector<CustomFigure>& figures);
std::string customGameplayDependenciesPatch(const std::vector<CustomFigure>& figures);
// Pure helpers for format and preservation tests.
std::string identifyCustomDna(std::string_view bytes, std::string_view marker);
void validateCustomLoadout(std::string_view toolTable, const CustomFigure& figure);
void resolveCustomInventory(std::string_view toolTable, CustomFigure& figure);
void resolveCustomControlSources(std::string_view toolTable, CustomFigure& figure);
std::string remapSlngLiteral(std::string_view script,std::string_view source,std::string_view replacement);
std::string customWeaponDefinitionsPatch(const std::vector<CustomFigure>& figures);
}
