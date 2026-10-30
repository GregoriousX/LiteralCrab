// SPDX-License-Identifier: GPL-3.0-only
#include "di1/toy_store_infinity.hpp"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace crabe::di1;
namespace crabe::di1 { void log(const char*, ...) {} }
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static bool run(lua_State* state, const std::string& code) {
    if (luaL_loadbuffer(state, code.data(), code.size(), "=toy-store-test") || lua_pcall(state, 0, 0, 0)) {
        std::printf("LUA: %s\n", lua_tostring(state, -1)); lua_pop(state, 1); return false;
    }
    return true;
}
static std::string patchCall(std::string_view patch, const char* result, const char* options) {
    return "(function(...)\n" + std::string(patch) + "\nend)(" + result + "," + options + ",function(m) logged[#logged+1]=m end,_G)";
}
static bool truth(lua_State* state, const char* expression) {
    const std::string code = "return " + std::string(expression);
    if (luaL_loadbuffer(state, code.data(), code.size(), "=check") || lua_pcall(state, 0, 1, 0)) {
        std::printf("LUA: %s\n", lua_tostring(state, -1)); lua_pop(state, 1); return false;
    }
    const bool value = lua_toboolean(state, -1) != 0; lua_pop(state, 1); return value;
}

// Independent stand-ins for the native menus, Catalog and bindings; no game script is used.
constexpr const char* world = R"lua(
logged, placed, used, playset, mission = {}, {}, 0, true, false
platform = "TARGET_WIN32"
function UI_IsWorldAPlayset() return playset end
function UI_GetZoneMgrBool() return false end
function UI_CurrentWorldName() return "Cars" end
function PlayerInActivity() return false end
function UI_IsInAdventure() return false end
function PlayerInExclusiveMission() return mission end
local function item(card, category, rro, tag, owned)
    return {isInventoryItem=true, recipeCardName=card, invItemName=card.."_inv", category=category, rroObjectName=rro,
        summonTag=tag, inventoryItemAvailable=owned, sortOrder=1}
end
-- inPlayset=false lists owned toys only; inPlayset=true also the locked ones.
function Place_GetInvItems(inPlayset, filter, player, exclude, itemExclude, filterMap, types, names)
    calls = (calls or 0) + 1; lastFilter = filter
    local list = {item("RC_Car", "CAT_VEHICLE", "cars_car", nil, true), item("RC_Horse", "CAT_ACTIONTOY", "lr_horse", "Horse", true),
        item("RC_Wheel", "CAT_PLAYSETDECORATION", "pir_wheel", nil, true), item("RC_Lamp", "CAT_BASICTOYS", "tbx_lamp", nil, true),
        item("RC_NoModel", "CAT_VEHICLE", "na", nil, true)}
    -- Toy Box versions have no recipe card; the horse's model is also the play set horse's.
    local elephant, horse = item("none", "CAT_VEHICLE", "tb_elephant", nil, true), item("none", "CAT_VEHICLE", "LR_Horse", nil, true)
    elephant.invItemName, horse.invItemName = "INV_TB_Elephant", "INV_LR_Horse"
    table.insert(list, 1, horse) list[#list+1] = elephant
    if inPlayset then list[#list+1] = item("RC_Ship", "CAT_VEHICLE", "pir_ship", "Ship", false) end
    for _, entry in ipairs(list) do
        if not types[entry.category] then types[entry.category] = {}; names[#names+1] = entry.category end
        table.insert(types[entry.category], entry)
    end
end
function Place_StartPlaceMode() end
function Place_CalculateSpawnPosition() end
function Place_CreateGhost(player, slot, rro) return rro end
function Place_PlaceObject(player, ghost) placed[#placed+1] = ghost end
function Place_StopPlaceMode() end
function Place_IncrementObjectsUsedCount() used = used + 1 end
function UI_GetItemDetails(table, card, field)
    if table == "RecipeCards" and field == "state" then
        if card == "RC_PI_SHIP" or card == "RC_PI_RAFT" or card == "RC_PI_NOLOCK" then return "" end -- Cards this world does not load.
        return card == "RC_MU_BIKE" and "PURCHASED" or "PURCHASABLE"
    end
end
-- Core defines every card's lock; another world's answers from the profile.
function Lock_IsLocked(name, player)
    if name == "RC_PI_SHIP" then return false elseif name == "RC_PI_RAFT" then return true end
end
RibbonData = {setTableAsClass = function(_, t) t.IsInventoryItem = function(s) return s.isInventoryItem end
    t.RecipeCardName = function(s) return s.recipeCardName end t.Category = function(s) return s.category end end}

Menu = {}
Menu.__index = Menu
function Menu.new(player) return setmetatable({InstanceData={playerNum=player}, screens={}, removed=0}, Menu) end
function Menu:AddScreen(name) self.screens[#self.screens+1] = name end
function Menu:RemoveScreen() self.removed = self.removed + 1 end
function Menu:GetItems() self.MenuOptions = {"Scn_Pause_Resume", "SharedText_Save", "Scn_Pause_Options", "SharedText_Quit"} return self.MenuOptions end
function Menu:ExitScreen() self.native = self.closeOption end
Kiosk = setmetatable({}, {__index = Menu})
Kiosk.__index = Kiosk
function Kiosk.new(player) return setmetatable(Menu.new(player), Kiosk) end
function Kiosk:GetItems() return {"Scn_Pause_MissionLog", "Scn_Pause_Catalog"} end

Catalog = {ITEM_STATE_LOCKED=1, ITEM_STATE_AVAILABLE=5, ITEM_STATE_PURCHASABLE=0, ACTION_STATE_NOT_AVAILABLE=0,
    ACTION_STATE_AVAILABLE=1, BldgBlock_None=0, InventoryExcludeList={}}
Catalog.__index = Catalog
function Catalog.new(player) return setmetatable({InstanceData={playerNum=player}}, Catalog) end
function Catalog:onEnter()
    self.inPlayset = UI_IsWorldAPlayset()
    self.TypeList, self.CatNameList, self.FullTypeListMap = {}, {}, {}
    self:LoadInventoryItems("cars", "Cars")
    self:LoadRecipeCardItems("cars", "Cars", {})
    for _, name in ipairs(self.CatNameList) do
        for _, entry in ipairs(self.TypeList[name]) do self.FullTypeListMap[entry:RecipeCardName()] = entry end
    end
end
function Catalog:LoadInventoryItems() end
function Catalog:LoadRecipeCardItems() self.TypeList.CAT_STORE = {{recipeCardName="RC_Native", RecipeCardName=function() return "RC_Native" end}}
    self.CatNameList[#self.CatNameList+1] = "CAT_STORE" end
function Catalog:onExit() cleared = 0 for _ in pairs(self.FullTypeListMap) do cleared = cleared + 1 end end
function Catalog:GetTitle() return "@Scn_Pause_Catalog" end
function Catalog:GetItemState() return self.ITEM_STATE_PURCHASABLE end
function Catalog:GetItemsCostAndType() return 25, "Coins" end
function Catalog:GetItemsAvailableCount() return 0 end
function Catalog:GetItemActionsStates() return "1|1|0|0" end
function Catalog:BuyItem() bought = true end
function Catalog:PurchaseItem() bought = true end
function Catalog:PlacePlaysetInventoryItem(name) Place_IncrementObjectsUsedCount(name) end
)lua";

static void menus(lua_State* state) {
    CHECK(run(state, patchCall(toyStoreInfinityMenuPatch, "Menu", "{Kiosk=false}")));
    CHECK(run(state, patchCall(toyStoreInfinityMenuPatch, "Menu", "{Kiosk=false}"))); // A recompiled chunk patches once.
    CHECK(run(state, patchCall(toyStoreInfinityMenuPatch, "Kiosk", "{Kiosk=true}")));
    CHECK(truth(state, "table.concat(Menu.new(0):GetItems(), ',') == "
        "'Scn_Pause_Resume,SharedText_Save,LiteralCrab_ToyStoreInfinity,Scn_Pause_Options,SharedText_Quit'"));
    CHECK(truth(state, "table.concat(Kiosk.new(0):GetItems(), ',') == "
        "'Scn_Pause_MissionLog,Scn_Pause_Catalog,LiteralCrab_ToyStoreInfinity'"));
    // Missions, the Toy Box and other menus keep the native options.
    CHECK(run(state, "mission = true"));
    CHECK(truth(state, "#Menu.new(0):GetItems() == 4"));
    CHECK(run(state, "mission = false playset = false"));
    CHECK(truth(state, "#Menu.new(0):GetItems() == 4"));
    CHECK(run(state, "playset = true"));
    CHECK(run(state, "pause = Menu.new(1) pause.closeOption = 'Scn_Pause_Options' pause:ExitScreen()"));
    CHECK(truth(state, "pause.native == 'Scn_Pause_Options' and #pause.screens == 0 and LiteralCrabToyStoreInfinity == nil"));
    CHECK(run(state, "pause.closeOption = 'LiteralCrab_ToyStoreInfinity' pause:ExitScreen()"));
    CHECK(truth(state, "pause.screens[1] == 'Catalog' and pause.removed == 0 and LiteralCrabToyStoreInfinity.player == 1"));
    CHECK(run(state, "kiosk = Kiosk.new(0) kiosk.closeOption = 'LiteralCrab_ToyStoreInfinity' kiosk:ExitScreen()"));
    CHECK(truth(state, "kiosk.screens[1] == 'Catalog' and kiosk.removed == 1 and LiteralCrabToyStoreInfinity.player == 0"));
}

static void store(lua_State* state, bool all) {
    CHECK(run(state, patchCall(toyStoreInfinityCatalogPatch, "Catalog", toyStoreCatalogOptions(all).c_str())));
    CHECK(run(state, patchCall(toyStoreInfinityCatalogPatch, "Catalog", "{All=false}")));
    // The native Toy Store is unchanged without a request, or with another player's request.
    CHECK(run(state, "LiteralCrabToyStoreInfinity = {player=1} native = Catalog.new(0) native:onEnter()"));
    CHECK(truth(state, "LiteralCrabToyStoreInfinity == nil and native.FullTypeListMap.RC_Native and not native.FullTypeListMap.RC_Car"));
    CHECK(truth(state, "native:GetTitle() == '@Scn_Pause_Catalog' and native:GetItemsCostAndType() == 25"));
    CHECK(run(state, "LiteralCrabToyStoreInfinity = {player=0} shop = Catalog.new(0) shop:onEnter()"));
    CHECK(truth(state, "LiteralCrabToyStoreInfinity == nil and shop.literalCrabInfinity and lastFilter == ''"));
    CHECK(truth(state, "shop.FullTypeListMap.RC_Car and shop.FullTypeListMap.RC_Horse and not shop.FullTypeListMap.RC_Native"));
    CHECK(truth(state, "not shop.FullTypeListMap.RC_Wheel and not shop.FullTypeListMap.RC_Lamp and not shop.FullTypeListMap.RC_NoModel"));
    CHECK(truth(state, "shop.FullTypeListMap.INV_TB_Elephant and not shop.FullTypeListMap.INV_LR_Horse and not shop.FullTypeListMap.none"));
    CHECK(truth(state, "shop:GetItemState('INV_TB_Elephant') == 5"));
    // Toy Box toys from the startup data: listed as unlocked, one per model, never invalid names.
    CHECK(truth(state, "shop:GetItemState('INV_TB_AutopiaCar') == 5 and not shop.FullTypeListMap.INV_LR_Silver"));
    CHECK(run(state, "placed = {} shop:PlacePlaysetInventoryItem('INV_TB_AutopiaCar')"));
    CHECK(truth(state, "placed[1] == 'LiteralCrab_TSI_TB_AutopiaCar' and used == 0"));
    // Another play set's toy: keyed by its recipe card, unlocked when the card was bought.
    CHECK(truth(state, "shop:GetItemState('RC_MU_BIKE') == 5 and shop.FullTypeListMap.RC_MU_BIKE.invItemName == 'INV_MU_Bike'"));
    CHECK(truth(state, all ? "shop:GetItemState('RC_MU_LOCKED') == 5" : "shop.FullTypeListMap.RC_MU_LOCKED == nil"));
    CHECK(truth(state, "shop:GetItemState('RC_PI_SHIP') == 5"));
    CHECK(truth(state, all ? "shop:GetItemState('RC_PI_RAFT') == 5 and shop:GetItemState('RC_PI_NOLOCK') == 5"
                           : "shop.FullTypeListMap.RC_PI_RAFT == nil and shop.FullTypeListMap.RC_PI_NOLOCK == nil"));
    CHECK(truth(state, all ? "shop.FullTypeListMap.RC_Ship and calls == 2" : "not shop.FullTypeListMap.RC_Ship and calls == 1"));
    CHECK(truth(state, "shop:GetTitle() == '@LiteralCrab_ToyStoreInfinity' and shop:GetItemsCostAndType() == 0"));
    CHECK(truth(state, "shop:GetItemState('RC_Car') == 5 and shop:GetItemActionsStates('RC_Car') == '1|0|1|0'"));
    CHECK(truth(state, "shop:GetItemsAvailableCount('RC_Car') == 1 and shop:GetItemState('RC_Native') == 1"));
    CHECK(truth(state, "shop:GetItemActionsStates('RC_Native') == '0|0|0|0'"));
    CHECK(run(state, "bought = false shop:BuyItem(1, 1, 0) shop:PurchaseItem('RC_Car')"));
    CHECK(truth(state, "bought == false"));
    CHECK(run(state, "used = 0 placed = {} shop:PlacePlaysetInventoryItem('RC_Horse') shop:PlacePlaysetInventoryItem('RC_Native')"));
    // A toy the store knows is placed through its session-only (M_NoSave) copy.
    CHECK(truth(state, "#placed == 1 and placed[1] == 'LiteralCrab_TSI_LR_HORSE' and used == 0"));
    if (all) {
        CHECK(run(state, "placed = {} shop:PlacePlaysetInventoryItem('RC_Ship')"));
        CHECK(truth(state, "placed[1] == 'pir_ship' and shop:GetItemState('RC_Ship') == 5"));
    }
    CHECK(run(state, "shop:onExit()"));
    CHECK(truth(state, "cleared == 0"));
    CHECK(run(state, "native:PlacePlaysetInventoryItem('RC_Native') native:onExit()"));
    CHECK(truth(state, "used == 1 and cleared == 1"));
    // Outside a play set the request is dropped.
    CHECK(run(state, "playset = false LiteralCrabToyStoreInfinity = {player=0} toybox = Catalog.new(0) toybox:onEnter() playset = true"));
    CHECK(truth(state, "not toybox.literalCrabInfinity and toybox.FullTypeListMap.RC_Native"));
}

// DICT: header {"DICT", 0x2000, checksum, 19, count, 1}, then {offset, 0, hash} per entry,
// strings one byte after entry + offset.
static std::string dictionaryFile(const std::vector<std::pair<std::uint32_t, std::string>>& entries) {
    std::string file("DICT", 4), strings;
    const auto put = [](std::string& out, std::uint32_t value) { out.append(reinterpret_cast<const char*>(&value), 4); };
    put(file, 0x2000); put(file, 1234); put(file, 19); put(file, static_cast<std::uint32_t>(entries.size())); put(file, 1);
    const std::size_t table = 24 + entries.size() * 12;
    std::vector<std::size_t> starts;
    for (const auto& [_, text] : entries) { starts.push_back(table + 1 + strings.size()); strings += text + '\0'; }
    for (std::size_t i = 0; i < entries.size(); ++i) {
        put(file, static_cast<std::uint32_t>(starts[i] - (24 + i * 12) - 1)); put(file, 0); put(file, entries[i].first);
    }
    return file + '\0' + strings;
}
static void generated(crabe::mods::AssetIndex& assets, const char* name, std::string value) {
    crabe::mods::AssetMember member;
    member.name = name; member.generated = std::make_shared<const std::string>(std::move(value));
    assets.members.push_back(std::move(member));
}
static void startupData() {
    const auto ids = dictionaryFile({{1, "inv_tb_autopiacar_name"}, {2, "inv_tb_autopiacar_desc"}, {3, "crocodile_name"}, {4, "other_name"}});
    CHECK(readDictionaryTexts(ids, dictionaryFile({{3, "Crocodile"}, {1, "Autopia Car"}, {4, "Other"}}),
        {"inv_tb_autopiacar_name", "crocodile_name"}) ==
        (std::map<std::string, std::string>{{"inv_tb_autopiacar_name", "Autopia Car"}, {"crocodile_name", "Crocodile"}}));
    bool rejected{};
    try { readDictionaryTexts(ids, "DICT", {}); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    auto otherBuild = ids; otherBuild[8] = '\x01';  // A language file of another dictionary build.
    rejected = false;
    try { readDictionaryTexts(ids, otherBuild, {}); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    auto broken = ids; broken[16] = '\x7f';
    rejected = false;
    try { readDictionaryTexts(broken, ids, {}); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);

    crabe::mods::AssetIndex assets;
    generated(assets, "gamedb/core/rumpusdata.lua", "Data={Records={{[\"Object Name\"]=\"LR_Silver\",Filename=\"LR_Silver\"}}}");
    generated(assets, "gamedb/core/rumpusroomdatamap.lua", "DataMap={Actor={LR_Silver={Files={'DNA|characters/LR_Silver.dnax',"
        "'Entity|characters/LR_Horse1/LR_Horse1'},Includes={'ts_bullseye.btx|Include'}}},"
        "Include={['ts_bullseye.btx']={Files={'AITree|Characters/TS_Bullseye.btx','DNA|characters/LR_Silver.dnax'}}}}");
    generated(assets, "gamedb/rumpusroom/inventory__lua.lua", "Data={Records={"
        "{name='INV_TB_AutopiaCar',type='CAT_VEHICLE',rroName='TB_AutopiaCar',enabled=1,sortOrder=7},"
        "{name='INV_LR_Silver',type='CAT_VEHICLE',rroName='LR_Silver',enabled=1,sortOrder=2},"
        "{name='INV_TB_TrainJupiter',type='CAT_VEHICLE',rroName='TB_TrainJupiter',enabled=1},"
        "{name='INV_TB_Off',type='CAT_VEHICLE',rroName='TB_AutopiaCar',enabled=0},"
        "{name='INV_NPC',type='CAT_NPC_PLAYSET',rroName='TB_Zebra',enabled=1},"
        "{name='INV_TB_Lost',type='CAT_VEHICLE',rroName='TB_Lost',enabled=1}}}");
    generated(assets, "gamedb/rumpusroom/rumpusdata.lua", "Data={Records={"
        "{[\"Object Name\"]=\"TB_AutopiaCar\",Id=2521,PackageName=\"RumpusRoom\"},"
        "{[\"Object Name\"]=\"TB_TrainJupiter\"},{[\"Object Name\"]=\"TB_Zebra\"}}}");
    // A Monsters U bike: its ride script and crash set come with the play set, not its actor.
    generated(assets, "gamedb/monsters/rumpusdata.lua", "Data={Records={"
        "{[\"Object Name\"]=\"MU_Bike\",Filename=\"MU_Bicycle\",PackageName=\"Monsters\"}}}");
    generated(assets, "gamedb/monsters/inventory__lua.lua", "Data={Records={"
        "{name='INV_MU_Bike',type='CAT_VEHICLE',rroName='MU_Bike',enabled=1,srcRCCard='RC_MU_BIKE',sortOrder=4}}}");
    generated(assets, "lang/tb/english_ntsc_win32.dct", dictionaryFile({{1, "Autopia Car"}, {2, "A car."}}));
    generated(assets, "lang/tb/polish_win32.dct", dictionaryFile({{1, "Samochód Autopia"}}));
    generated(assets, "lang/tb/stringids_win32.dct", ids);
    // Main dictionary: two pause menu texts, English and Polish both say "OK" for one of them.
    generated(assets, "lang/main/stringids_win32.dct", dictionaryFile({{7, "scn_pause_options"}, {8, "sharedtext_quit"}}));
    generated(assets, "lang/main/english_ntsc_win32.dct", dictionaryFile({{7, "Options"}, {8, "OK"}}));
    generated(assets, "lang/main/polish_win32.dct", dictionaryFile({{7, "Opcje"}, {8, "OK"}}));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    NativeCatalog catalog(assets, L"");
    std::vector<std::string> messages;
    auto data = readToyStoreData(catalog, messages);
    CHECK(data.toys.size() == 3 && data.toys[0].name == "INV_TB_AutopiaCar" && data.toys[0].model == "TB_AutopiaCar" &&
          data.toys[0].sortOrder == 7 && data.toys[0].toyBox && data.toys[0].card.empty() && data.toys[1].name == "INV_LR_Silver" &&
          data.toys[2].card == "RC_MU_BIKE" && !data.toys[2].toyBox);
    CHECK(data.keys.contains("inv_tb_autopiacar_desc") && data.keys.contains("inv_lr_silver_name"));
    CHECK(data.dependencies == (std::vector<std::string>{"DNA|characters/LR_Silver.dnax", "AITree|Characters/TS_Bullseye.btx",
        "Script|scripts/aiinteractionriding.sx", "Entity|vehicles/destructionbicycle"}));
    // The Toy Box row the play sets lack, and session-only copies of every toy's row (Core's too).
    CHECK(data.objects.find("[\"Object Name\"]=\"TB_AutopiaCar\"") != std::string::npos &&
          data.objects.find("[\"Object Name\"]=\"LR_Silver\"") == std::string::npos && data.objects.find("TB_Zebra") == std::string::npos);
    CHECK(data.objects.find("[\"Object Name\"]=\"LiteralCrab_TSI_TB_AutopiaCar\"") != std::string::npos &&
          data.objects.find("[\"Object Name\"]=\"LiteralCrab_TSI_LR_Silver\"") != std::string::npos);
    CHECK(data.objects.find("M_NoSave") != std::string::npos && data.objects.find("990001") != std::string::npos);
    CHECK(data.languages.size() == 2 && data.languages[0].dictionary == "english_ntsc" &&
          data.languages[1].texts.at("inv_tb_autopiacar_name") == "Samochód Autopia" &&
          data.languages[1].probes.at("scn_pause_options") == "Opcje" && data.languages[0].probes.size() == 2);
    CHECK(messages.size() == 1 && messages[0].find("2 Toy Box and 1 play set vehicles/mounts, 3 session-only and 2 original object rows and 4 dependency files") != std::string::npos);
    setToyStoreData(data);
    CHECK(toyStoreCatalogOptions(false).find("{name='INV_TB_AutopiaCar',model='TB_AutopiaCar',placed='LiteralCrab_TSI_TB_AutopiaCar',card='',sort=7,toyBox=true}") != std::string::npos);
    CHECK(toyStoreObjectOptions().starts_with("{Packages={") && toyStoreObjectOptions().find(",Objects={{") != std::string::npos);

    // The game's own text wins; a missing one ("[key]") comes from the language whose pause menu
    // texts match the game's.
    struct Native { const char* options; int calls; } polish{"Opcje", 0};
    const NativeText missing = [](void* context, const char* key) -> const char* {
        auto* native = static_cast<Native*>(context); ++native->calls;
        if (!std::strcmp(key, "scn_pause_options")) return native->options;
        return std::strcmp(key, "sharedtext_quit") ? "[missing]" : "OK";
    };
    CHECK(std::strcmp(toyStoreToyText("INV_TB_AutopiaCar_name", missing, &polish), "Samochód Autopia") == 0);
    CHECK(std::strcmp(toyStoreToyText("INV_TB_AutopiaCar_desc", missing, &polish), "A car.") == 0); // English stands in.
    CHECK(std::strcmp(toyStoreToyText("INV_LR_Silver_desc", missing, &polish), "") == 0); // No description at all.
    CHECK(!toyStoreToyText("INV_LR_Silver_name", missing, &polish));
    // A recipe card without a text of its own shows its inventory item's.
    auto carded = data;
    carded.toys.push_back({"INV_TB_AutopiaCar_LR", "TB_AutopiaCar", "RC_Autopia", 1, false});
    carded.keys.insert({"rc_autopia_name", "rc_autopia_desc"});
    carded.aliases.emplace("rc_autopia_name", "inv_tb_autopiacar_name");
    setToyStoreData(carded);
    CHECK(std::strcmp(toyStoreToyText("RC_Autopia_name", missing, &polish), "Samochód Autopia") == 0);
    CHECK(!toyStoreToyText("SharedText_Ok", missing, &polish) && !toyStoreToyText("scn_pause_options", missing, &polish));
    const NativeText present = [](void*, const char*) -> const char* { return "Autopia"; };
    CHECK(!toyStoreToyText("INV_TB_AutopiaCar_name", present, nullptr));
    setToyStoreData(data);
    Native unknown{"???", 0};
    CHECK(std::strcmp(toyStoreToyText("@INV_TB_AutopiaCar_desc", missing, &unknown), "A car.") == 0); // English fallback.

    // The object patch adds missing rows under the play set's package name.
    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    CHECK(run(state, "logged = {} Data = {PackageName='Incredibles', Records={{['Object Name']='IN_MRINC_CAR'}}}"));
    const auto options = "{Packages={Incredibles=true},Objects={{['Object Name']='TB_AutopiaCar',PackageName='RumpusRoom'},{['Object Name']='in_mrinc_car'}}}";
    CHECK(run(state, patchCall(toyStoreInfinityObjectsPatch, "nil", options)));
    CHECK(truth(state, "#Data.Records == 2 and Data.Records[2]['Object Name'] == 'TB_AutopiaCar' and Data.Records[2].PackageName == 'Incredibles'"));
    CHECK(truth(state, "logged[1] == 'APPLIED: 1 vehicle objects of other worlds for Toy Store Infinity in Incredibles'"));
    // Core, the Toy Box and hubs are left alone.
    CHECK(run(state, "logged = {} Data = {PackageName='Core', Records={}}"));
    CHECK(run(state, patchCall(toyStoreInfinityObjectsPatch, "nil", options)));
    CHECK(truth(state, "#Data.Records == 0 and #logged == 0"));
    CHECK(toyStoreObjectOptions().find("Packages={Cars=true,Incredibles=true,") != std::string::npos &&
          toyStoreObjectOptions().find("Pirates=true") != std::string::npos);
    // Pirates: the generated empty package is valid data and joins the list that loads its cards, once.
    CHECK(run(state, std::string(toyStorePiratesObjects) + " assert(Data.PackageName == 'Pirates' and #Data.Records == 0)"));
    CHECK(run(state, "logged = {} DataMap = {Zone={pirates={Files={'GameDB|GameDB/Pirates/locks__lua.lua|1|locks',"
        "'GameDB|GameDB/Pirates/rc_catalog__lua.lua|1|RC_Catalog'}}, other={Files={'GameDB|GameDB/Pirates/stats.lua|1|stats'}}}}"));
    CHECK(run(state, patchCall(toyStoreInfinityPiratesPatch, "nil", "{}")));
    CHECK(run(state, patchCall(toyStoreInfinityPiratesPatch, "nil", "{}")));
    CHECK(truth(state, "#DataMap.Zone.pirates.Files == 3 and DataMap.Zone.pirates.Files[3] == 'GameDB|GameDB/Pirates/rumpusdata.lua|1|RumpusData'"));
    CHECK(truth(state, "#DataMap.Zone.other.Files == 1 and #logged == 1"));
    // The avatar's bike ride perception opens to every play set, once; other entries stay.
    CHECK(run(state, "logged = {} BehaviorTree = {Children={{Mods={{Perceptions={{'rumpus,adv_','RideCar',0},"
        "{'mu_','SummonBike',0},{'mu_','RideBicycle',0},{'mu_',0,'RideBicycle'}}}}}}}"));
    CHECK(run(state, patchCall(toyStoreInfinityRidingPatch, "nil", "{}")));
    CHECK(run(state, patchCall(toyStoreInfinityRidingPatch, "nil", "{}")));
    CHECK(truth(state, "(function() local p = BehaviorTree.Children[1].Mods[1].Perceptions "
        "return p[1][1] == 'rumpus,adv_' and p[2][1] == 'mu_' and p[3][1] == 'mu_,in_,ca_,lr_,pc_,ts_' and p[4][1] == p[3][1] end)()"));
    CHECK(truth(state, "#logged == 1 and logged[1]:find('APPLIED: bikes can be ridden') == 1"));
    CHECK(run(state, "BehaviorTree = nil logged = {}"));
    CHECK(run(state, patchCall(toyStoreInfinityRidingPatch, "nil", "{}")));
    CHECK(truth(state, "#logged == 0"));
    // Mount dependencies (not models) join the include every play set loads, once.
    auto withFiles = data;
    withFiles.dependencies = {"Script|characters/ridablemoveset.btl.sx", "AnimTreeProject|characters/TS_Bullseye/TS_Bullseye.animtreeproject", "Bad'quote"};
    setToyStoreData(withFiles);
    const auto files = toyStoreDependencyOptions();
    CHECK(files.find("'Script|characters/ridablemoveset.btl.sx'") != std::string::npos && files.find("Bad") == std::string::npos);
    CHECK(run(state, "logged = {} DataMap = {Playset={_loadforallplaysets_={Includes={'avatarstats|Include'}}}}"));
    CHECK(run(state, patchCall(toyStoreInfinityDependenciesPatch, "nil", files.c_str())));
    CHECK(run(state, patchCall(toyStoreInfinityDependenciesPatch, "nil", files.c_str())));
    CHECK(truth(state, "#DataMap.Playset._loadforallplaysets_.Includes == 2 and #DataMap.Include.literalcrab_toy_store.Files == 2"));
    CHECK(truth(state, "#logged == 1 and logged[1]:find('APPLIED: 2 dependency files') == 1"));
    CHECK(run(state, "Data = nil logged = {}"));
    CHECK(run(state, patchCall(toyStoreInfinityObjectsPatch, "nil", options)));
    CHECK(truth(state, "logged[1]:find('FALLBACK') == 1"));
    lua_close(state);
}

int main() {
    startupData();
    setToyStoreData({{{"INV_TB_AutopiaCar", "TB_AutopiaCar", "", 3, true}, {"INV_LR_Silver", "LR_HORSE", "", 1, true},
        {"bad name", "x", "", 0, true}, {"INV_MU_Bike", "MU_Bike", "RC_MU_BIKE", 5, false},
        {"INV_MU_Locked", "MU_Locked", "RC_MU_LOCKED", 6, false}, {"INV_PI_Ship", "PI_Ship", "RC_PI_SHIP", 7, false},
        {"INV_PI_Raft", "PI_Raft", "RC_PI_RAFT", 8, false}, {"INV_PI_NoLock", "PI_NoLock", "RC_PI_NOLOCK", 9, false}}, "{}", {}, {}});
    CHECK(std::strcmp(toyStoreInfinityText("@LiteralCrab_ToyStoreInfinity"), "Toy Store Infinity") == 0);
    CHECK(std::strcmp(toyStoreInfinityText("literalcrab_toystoreinfinity"), "Toy Store Infinity") == 0);
    CHECK(!toyStoreInfinityText("Scn_Pause_Catalog") && !toyStoreInfinityText(nullptr));
    for (const bool all : {false, true}) {
        lua_State* state = luaL_newstate();
        luaL_openlibs(state);
        CHECK(run(state, world));
        menus(state);
        store(state, all);
        CHECK(run(state, "counted = {} for _, m in ipairs(logged) do counted[m] = true end"));
        CHECK(truth(state, all ? "counted['Toy Store Infinity: 10 toys (all)']" : "counted['Toy Store Infinity: 6 unlocked toys']"));
        lua_close(state);
    }
    // Missing native pieces leave the screens alone.
    lua_State* state = luaL_newstate();
    luaL_openlibs(state);
    CHECK(run(state, "logged = {} Broken = {}"));
    CHECK(run(state, patchCall(toyStoreInfinityCatalogPatch, "Broken", "{}")));
    CHECK(run(state, patchCall(toyStoreInfinityMenuPatch, "Broken", "{}")));
    CHECK(truth(state, "#logged == 2 and logged[1]:find('FALLBACK') == 1 and next(Broken) == nil"));
    lua_close(state);
    if (failures) std::printf("%u failure(s)\n", failures);
    else std::puts("toy store infinity tests passed");
    return failures ? 1 : 0;
}
