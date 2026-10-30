// SPDX-License-Identifier: GPL-3.0-only
#include "di1/toy_store_infinity.hpp"
#include "di1/common.hpp"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace crabe::di1 {
namespace {
ToyStoreData toyStore;
const ToyStoreLanguage* language{};
bool languageChosen{};
const ToyStoreLanguage* english() {
    for (const auto& candidate : toyStore.languages) if (candidate.dictionary == "english_ntsc") return &candidate;
    return nullptr;
}
std::string lowercase(std::string_view value) {
    std::string result(value);
    for (auto& c : result) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return result;
}
// Inventory and object names are identifiers; anything else is not offered to Lua.
bool identifier(std::string_view value) {
    if (value.empty()) return false;
    for (const char c : value) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    return true;
}
}

const char* toyStoreInfinityText(const char* key) noexcept {
    if (!key) return nullptr;
    if (*key == '@') ++key;
    return _stricmp(key, toyStoreInfinityKey.data()) ? nullptr : "Toy Store Infinity";
}

void setToyStoreData(ToyStoreData data) {
    toyStore = std::move(data);
    language = nullptr;
    languageChosen = false;
}

const char* toyStoreToyText(const char* key, NativeText native, void* context) {
    if (!key || toyStore.languages.empty()) return nullptr;
    if (*key == '@') ++key;
    const auto wanted = lowercase(key);
    // Only the store's toy keys, and only when the game has no text of its own.
    if (!toyStore.keys.contains(wanted)) return nullptr;
    // Missing natively: no text, "[key]" or the key itself.
    if (const auto* text = native(context, key); text && *text && *text != '[' && _stricmp(text, key)) return nullptr;
    if (!languageChosen) {
        languageChosen = true;
        // The language whose pause menu texts match the game's most; ties keep the first.
        std::string seen[std::size(toyStoreProbeKeys)];
        for (std::size_t i = 0; i < std::size(toyStoreProbeKeys); ++i)
            if (const auto* text = native(context, toyStoreProbeKeys[i].data())) seen[i] = text;
        std::size_t best{};
        for (const auto& candidate : toyStore.languages) {
            std::size_t matches{};
            for (std::size_t i = 0; i < std::size(toyStoreProbeKeys); ++i)
                if (const auto found = candidate.probes.find(std::string(toyStoreProbeKeys[i]));
                    found != candidate.probes.end() && !seen[i].empty() && found->second == seen[i]) ++matches;
            if (matches > best) { best = matches; language = &candidate; }
        }
        if (!language) language = english();
        if (!language) language = &toyStore.languages.front();
        log("ToyStore: names of other worlds' toys in %s.", language->dictionary.c_str());
    }
    // The key, then a card's inventory item; in the game's language, then in English. A missing
    // description shows nothing rather than its key.
    const auto alias = toyStore.aliases.find(wanted);
    const std::string* keys[] = {&wanted, alias == toyStore.aliases.end() ? nullptr : &alias->second};
    const ToyStoreLanguage* languages[] = {language, english()};
    for (const auto* candidate : languages)
        for (const auto* text : keys)
            if (candidate && text)
                if (const auto found = candidate->texts.find(*text); found != candidate->texts.end()) return found->second.c_str();
    return wanted.ends_with("_desc") ? "" : nullptr;
}

std::string toyStoreCatalogOptions(bool all) {
    std::string toys;
    for (const auto& toy : toyStore.toys) {
        if (!identifier(toy.name) || !identifier(toy.model) || (!toy.card.empty() && !identifier(toy.card))) continue;
        char sort[32]{};
        std::snprintf(sort, sizeof sort, "%.0f", toy.sortOrder);
        toys += "{name='" + toy.name + "',model='" + toy.model + "',placed='" + toyStorePlacedName(toy.model) + "',card='" + toy.card + "',sort=" + sort +
            (toy.toyBox ? ",toyBox=true}," : "},");
    }
    return std::string("{All=") + (all ? "true" : "false") + ",Toys={" + toys + "}}";
}

std::string toyStoreObjectOptions() {
    if (toyStore.objects.size() <= 2) return {};
    std::string packages;
    for (const auto package : toyStorePackages) packages += std::string(package) + "=true,";
    return "{Packages={" + packages + "},Objects=" + toyStore.objects + "}";
}

std::string toyStoreDependencyOptions() {
    std::string files;
    for (const auto& file : toyStore.dependencies) {
        // Data-map entries are "Kind|path[|n]"; anything else is not offered to Lua.
        if (file.empty() || std::any_of(file.begin(), file.end(), [](char c) {
                return !std::isalnum(static_cast<unsigned char>(c)) && !std::strchr("_./|- ", c); })) continue;
        files += "'" + file + "',";
    }
    return files.empty() ? std::string{} : "{Files={" + files + "}}";
}

// Shared by the pause menu and the Infinity kiosk; options.Kiosk selects the kiosk's flow.
// The request travels to the Catalog through a global that the Catalog clears on entry.
const std::string_view toyStoreInfinityMenuPatch = R"lua(
local menu, options, log = ...
if type(menu) ~= "table" or type(menu.GetItems) ~= "function" or type(menu.ExitScreen) ~= "function" then
    log("FALLBACK: menu not found; Toy Store Infinity has no entry")
    return
end
-- Own field only: menus are classes, and a patched base class must not hide this one.
if rawget(menu, "literalCrabToyStoreInfinity") then return end
menu.literalCrabToyStoreInfinity = true
local entry, kiosk = "LiteralCrab_ToyStoreInfinity", options.Kiosk == true
-- The pause menu's ordinary play set branch only: not intros, activities, adventures or missions.
local function ordinaryPlayset(self)
    local ok, result = pcall(function()
        local player = self.InstanceData.playerNum
        return platform ~= "TARGET_IOS" and platform ~= "TARGET_WIN8"
            and UI_IsWorldAPlayset() == true
            and not UI_GetZoneMgrBool(UI_CurrentWorldName(), "worldIsIntro")
            and not PlayerInActivity(player) and not UI_IsInAdventure()
            and not PlayerInExclusiveMission(player)
    end)
    return ok and result == true
end
local getItems, exitScreen = menu.GetItems, menu.ExitScreen
menu.GetItems = function(self, ...)
    local items = getItems(self, ...)
    if type(items) ~= "table" then return items end
    local at
    for index, option in ipairs(items) do
        if option == entry then return items end
        if kiosk and option == "Scn_Pause_Catalog" then at = index + 1 end
        if not kiosk and option == "Scn_Pause_Options" and not at then at = index end
    end
    if at and (kiosk or ordinaryPlayset(self)) then table.insert(items, at, entry) end
    return items
end
menu.ExitScreen = function(self, ...)
    if self.closeOption ~= entry then return exitScreen(self, ...) end
    if kiosk then self:RemoveScreen() end
    LiteralCrabToyStoreInfinity = {player = self.InstanceData.playerNum}
    self:AddScreen("Catalog")
end
log("APPLIED: Toy Store Infinity entry")
)lua";

// Infinity mode of the play set Toy Store. The native screen builds its list from what
// LoadInventoryItems puts into TypeList; in this mode that is every spawnable inventory toy
// (CAT_VEHICLE or a summon tag: vehicles and mounts) from Place_GetInvItems with no world
// filter. Buying, recipe cards, "new" flags and used counts are left alone.
const std::string_view toyStoreInfinityCatalogPatch = R"lua(
local catalog, options, log = ...
if type(catalog) ~= "table" or type(catalog.onEnter) ~= "function" or type(catalog.LoadInventoryItems) ~= "function"
    or type(catalog.GetItemActionsStates) ~= "function" or type(Place_GetInvItems) ~= "function" then
    log("FALLBACK: Toy Store not found; Toy Store Infinity disabled")
    return
end
if rawget(catalog, "literalCrabToyStoreInfinity") then return end
catalog.literalCrabToyStoreInfinity = true
local all = options.All == true
-- Catalog categories that are never spawned beside the player (play set lots, sockets, tracks).
local fixed = {CAT_TRAIN = true, CAT_BUILDING = true, CAT_BUILDINGSETS = true, CAT_BUILDINGOBJS = true,
    CAT_PLAYSETDECORATION = true, CAT_DECORATION = true, CAT_PLAYSETS = true}
local function named(value)
    return type(value) == "string" and value ~= "" and string.lower(value) ~= "none" and string.lower(value) ~= "na"
end
local function available(item) return item.inventoryItemAvailable == true or item.inventoryItemAvailable == 1 end
local function spawnable(item)
    if type(item) ~= "table" or not item.isInventoryItem or not named(item.rroObjectName)
        or not (named(item.recipeCardName) or named(item.invItemName)) then
        return false
    end
    local category = string.upper(tostring(item.category or ""))
    if fixed[category] then return false end
    return category == "CAT_VEHICLE" or named(item.summonTag)
end
-- Only the current play set's and the Toy Box's inventory are loaded, so the vehicles of other
-- play sets come from their Toy Box versions (INV_LR_Silver beside INV_LR_Silver_LR). Those
-- have no recipe card; like the Toy Box ribbon they are keyed by their inventory name, whose
-- "<name>_name" text and icon the game already has. One entry per model, an unlocked one first.
local function better(item, current)
    if available(item) ~= available(current) then return available(item) end
    return named(item.recipeCardName) and not named(current.recipeCardName)
end
local function collect(self)
    local found, list = {}, {}
    -- inPlayset=true also lists locked toys that have a recipe card; only needed for InfinityAll.
    for _, inPlayset in ipairs(all and {false, true} or {false}) do
        local types, names = {}, {}
        Place_GetInvItems(inPlayset, "", self.InstanceData.playerNum, self.InventoryExcludeList, nil, nil, types, names, {}, false)
        for _, name in ipairs(names) do
            for _, item in ipairs(types[name] or {}) do
                if spawnable(item) and (all or available(item)) then
                    local model = string.lower(item.rroObjectName)
                    if not found[model] then
                        found[model] = #list + 1
                        list[#list + 1] = item
                    elseif better(item, list[found[model]]) then
                        list[found[model]] = item
                    end
                end
            end
        end
    end
    -- Toys of the Toy Box (all unlocked in Gold) and of the other play sets, read from the game
    -- files at startup: only this world's are in its loaded data. Another play set's toy is
    -- unlocked when the game says so, as for tools: its recipe card's state where this world
    -- loads the card, else the card's lock (Core defines every card's lock; for another world's
    -- lock the game answers from the player's profile). A card without either stays hidden.
    local locked = 0
    for _, toy in ipairs(type(options.Toys) == "table" and options.Toys or {}) do
        local model, unlocked = string.lower(toy.model), toy.toyBox == true
        if not unlocked and named(toy.card) then
            local ok, state = false, nil
            if type(UI_GetItemDetails) == "function" then ok, state = pcall(UI_GetItemDetails, "RecipeCards", toy.card, "state") end
            if ok and named(state) then
                unlocked = state == "PURCHASED" or state == "DELIVERED" or state == "AVAILABLE"
            elseif type(Lock_IsLocked) == "function" then
                local known, isLocked = pcall(Lock_IsLocked, toy.card, self.InstanceData.playerNum)
                unlocked = known and isLocked == false
            end
            if not unlocked then locked = locked + 1 end
        end
        local item = {isInventoryItem = true, invItemName = toy.name, recipeCardName = named(toy.card) and toy.card or toy.name,
            category = "CAT_VEHICLE", rroObjectName = toy.model, inventoryItemAvailable = unlocked, sortOrder = toy.sort or 0,
            milestoneLevel = 0, timesPlaced = 0}
        if not (all or unlocked) then
        elseif not found[model] then
            found[model] = #list + 1
            list[#list + 1] = item
        elseif unlocked and not available(list[found[model]]) then
            list[found[model]] = item
        end
    end
    -- Every toy the store knows is placed through its session-only copy (M_NoSave), also the
    -- current play set's own: nothing the store places enters a save.
    local placed = {}
    for _, toy in ipairs(type(options.Toys) == "table" and options.Toys or {}) do
        if named(toy.placed) then placed[string.lower(toy.model)] = toy.placed end
    end
    for _, item in ipairs(list) do
        if not named(item.recipeCardName) then item.recipeCardName = item.invItemName end
        item.literalCrabPlace = placed[string.lower(item.rroObjectName)]
    end
    if locked > 0 then log("Toy Store Infinity: " .. locked .. " toys of other play sets are still locked" .. (all and " (listed)" or "")) end
    return list
end
local function infinity(self) return self.literalCrabInfinity == true end
local function wrap(name, replacement)
    local original = catalog[name]
    if type(original) ~= "function" then return end
    catalog[name] = function(self, ...)
        if infinity(self) then return replacement(self, original, ...) end
        return original(self, ...)
    end
end

local onEnter = catalog.onEnter
catalog.onEnter = function(self, ...)
    local request = LiteralCrabToyStoreInfinity
    LiteralCrabToyStoreInfinity = nil
    self.literalCrabInfinity = type(request) == "table" and request.player == self.InstanceData.playerNum
        and UI_IsWorldAPlayset() == true
    return onEnter(self, ...)
end
wrap("LoadInventoryItems", function(self)
    local data = RibbonData or require("RibbonData")
    local items = collect(self)
    for _, item in ipairs(items) do
        if data and data.setTableAsClass then data:setTableAsClass(item) end
        local category = item.category
        if self.TypeList[category] == nil then
            self.TypeList[category] = {}
            self.CatNameList[#self.CatNameList + 1] = category
        end
        local list = self.TypeList[category]
        list[#list + 1] = item
    end
    log("Toy Store Infinity: " .. #items .. (all and " toys (all)" or " unlocked toys"))
    local keys = {}
    for index = 1, math.min(#items, 80) do keys[index] = items[index].recipeCardName end
    if #keys > 0 then log("Toy Store Infinity list: " .. table.concat(keys, ", ")) end
end)
wrap("LoadRecipeCardItems", function() end)
wrap("onExit", function(self, original, ...)
    -- The native exit clears the "new" flag of every listed recipe card, also other play sets'.
    self.FullTypeListMap = {}
    return original(self, ...)
end)
wrap("GetTitle", function() return "@LiteralCrab_ToyStoreInfinity" end)
wrap("GetItemState", function(self, original, id)
    local item = self.FullTypeListMap[id]
    if item ~= nil and (all or available(item)) then return self.ITEM_STATE_AVAILABLE end
    return self.ITEM_STATE_LOCKED
end)
wrap("GetItemsCostAndType", function() return 0, "" end)
wrap("GetItemsAvailableCount", function(self, original, id) return self.FullTypeListMap[id] and 1 or 0 end)
wrap("GetItemActionsStates", function(self, original, id)
    self.currentSelectedItemName = id
    self.BlockingBuildingPlacement = self.BldgBlock_None
    self.Flash_BlockingBuildingPlacement = self.BlockingBuildingPlacement
    local place = self:GetItemState(id) == self.ITEM_STATE_AVAILABLE
    return string.format("%d|%d|%d|%d", place and 1 or 0, self.ACTION_STATE_NOT_AVAILABLE,
        place and self.ACTION_STATE_AVAILABLE or self.ACTION_STATE_NOT_AVAILABLE, self.ACTION_STATE_NOT_AVAILABLE)
end)
wrap("BuyItem", function() end)
wrap("PurchaseItem", function() end)
wrap("PlacePlaysetInventoryItem", function(self, original, itemName)
    -- The native placement without Place_IncrementObjectsUsedCount: nothing is consumed.
    local item = self.FullTypeListMap[itemName]
    if item == nil or self:GetItemState(itemName) ~= self.ITEM_STATE_AVAILABLE then return end
    local player = self.InstanceData.playerNum
    Place_StartPlaceMode(player)
    Place_CalculateSpawnPosition(player)
    local ghost
    if self.creationLocationSpecified then
        ghost = Place_CreateGhostAtLocation(player, 0, item.literalCrabPlace or item.rroObjectName, self.creationLocationX, self.creationLocationY, self.creationLocationZ)
    else
        ghost = Place_CreateGhost(player, 0, item.literalCrabPlace or item.rroObjectName)
    end
    Place_PlaceObject(player, ghost)
    Place_StopPlaceMode(player, 0, false)
    log("Toy Store Infinity: placed " .. tostring(itemName) .. " (" .. tostring(item.literalCrabPlace or item.rroObjectName) .. ", ghost " .. tostring(ghost) .. ")")
end)
log("APPLIED: Toy Store Infinity" .. (all and " (all toys)" or ""))
)lua";

// A play set loads its own RumpusData package and Core only, so another world's vehicle has no
// object row (what Place_CreateGhost builds from). The rows are copied into each play set
// package (options.Packages); Core, the Toy Box and the hubs keep theirs. Rows the package
// already has are kept; the copies take the package's name.
const std::string_view toyStoreInfinityObjectsPatch = R"lua(
local _, options, log, environment = ...
local data = environment and environment.Data
if type(data) ~= "table" or type(data.Records) ~= "table" or type(options.Objects) ~= "table" then
    log("FALLBACK: object data not found; other worlds' vehicles cannot be placed here")
    return
end
if type(options.Packages) ~= "table" or not options.Packages[data.PackageName] then return end
local present, added = {}, 0
for _, row in ipairs(data.Records) do
    if type(row) == "table" and type(row["Object Name"]) == "string" then present[string.lower(row["Object Name"])] = true end
end
for _, row in ipairs(options.Objects) do
    local name = type(row) == "table" and row["Object Name"]
    if type(name) == "string" and not present[string.lower(name)] then
        local copy = {}
        for key, value in pairs(row) do copy[key] = value end
        copy.PackageName = data.PackageName or copy.PackageName
        data.Records[#data.Records + 1] = copy
        present[string.lower(name)] = true
        added = added + 1
    end
end
log("APPLIED: " .. added .. " vehicle objects of other worlds for Toy Store Infinity in " .. tostring(data.PackageName))
)lua";

// Pirates lists its game data (stats, locks, cards) but no object package, so nothing from
// another world could be placed there. The generated empty package joins every file list
// that loads the Pirates recipe cards; the object patch then fills it like the others.
const std::string_view toyStoreInfinityPiratesPatch = R"lua(
local _, options, log = ...
if type(DataMap) ~= "table" then return end
local cards, objects = "gamedb|gamedb/pirates/rc_catalog__lua.lua|1|rc_catalog", "GameDB|GameDB/Pirates/rumpusdata.lua|1|RumpusData"
local added, seen = 0, {}
local function visit(value)
    if type(value) ~= "table" or seen[value] then return end
    seen[value] = true
    local hasCards, hasObjects = false, false
    for _, entry in ipairs(value) do
        if type(entry) == "string" then
            local key = string.lower(entry)
            hasCards = hasCards or key == cards
            hasObjects = hasObjects or key == string.lower(objects)
        end
    end
    if hasCards and not hasObjects then value[#value + 1] = objects; added = added + 1 end
    for _, child in pairs(value) do visit(child) end
end
visit(DataMap)
if added > 0 then log("APPLIED: Pirates loads an object package for Toy Store Infinity (" .. added .. " list(s))") end
)lua";

// The avatar perceives an interaction only in zones whose name starts with one of its
// entry's prefixes; RideBicycle is "mu_", so a bike placed in another world could not be
// mounted. Its ride entries get the other play sets' prefixes (SummonBike, MU's own bike
// post, stays as it is).
const std::string_view toyStoreInfinityRidingPatch = R"lua(
local _, options, log = ...
if type(BehaviorTree) ~= "table" then return end
local wanted, opened, seen = {"mu_", "in_", "ca_", "lr_", "pc_", "ts_"}, 0, {}
local function widen(entry)
    local prefixes, present = entry[1], {}
    for prefix in string.gmatch(string.lower(prefixes), "[^,]+") do present[prefix] = true end
    if not present["mu_"] then return end
    for _, prefix in ipairs(wanted) do
        if not present[prefix] then prefixes = prefixes .. "," .. prefix end
    end
    if prefixes ~= entry[1] then entry[1] = prefixes; opened = opened + 1 end
end
local function visit(value)
    if type(value) ~= "table" or seen[value] then return end
    seen[value] = true
    if type(value[1]) == "string" and (value[2] == "RideBicycle" or value[3] == "RideBicycle") then widen(value) return end
    for _, child in pairs(value) do visit(child) end
end
visit(BehaviorTree)
if opened > 0 then log("APPLIED: bikes can be ridden in every play set (" .. opened .. " avatar perception entries)") end
)lua";

// A play set loads the dependencies of its own actors only, so another world's mount had its
// model but no animation tree, behavior or ride scripts. Those files (never the models, which
// stream on placement) join the include every play set loads.
const std::string_view toyStoreInfinityDependenciesPatch = R"lua(
local _, options, log = ...
local playsets = type(DataMap) == "table" and DataMap.Playset
local common = type(playsets) == "table" and playsets._loadforallplaysets_
if type(common) ~= "table" or type(options.Files) ~= "table" then return end
if type(DataMap.Include) ~= "table" then DataMap.Include = {} end
DataMap.Include.literalcrab_toy_store = {Files = options.Files}
if type(common.Includes) ~= "table" then common.Includes = {} end
for _, value in ipairs(common.Includes) do if value == "literalcrab_toy_store|Include" then return end end
common.Includes[#common.Includes + 1] = "literalcrab_toy_store|Include"
log("APPLIED: " .. #options.Files .. " dependency files of other worlds' vehicles and mounts load in every play set")
)lua";
}
