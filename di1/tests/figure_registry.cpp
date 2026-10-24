// SPDX-License-Identifier: GPL-3.0-only
// DI1 manifest policy, original ciphertext vectors and the generated gateway patch in Lua 5.1.
#include "di1/figure_registry.hpp"
#include <windows.h>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
extern "C" {
#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
}

using namespace crabe::di1;
namespace fs = std::filesystem;
static int failures{};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static std::string hex(std::string_view bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const unsigned char byte : bytes) {
        result += digits[byte >> 4]; result += digits[byte & 15];
    }
    return result;
}

static void cryptoVectors() {
    // Byte-for-byte vectors from the original DI1 gateway.lua extraction:
    // gateway-key-values.tsv, Category1 rows 1, 2 and 38. These are independent
    // known answers, including a two-block value, row IV changes and LE doubles.
    CHECK(hex(customFigureGatewayMarker()) == "22139f76a39058388c724022ef07df78");
    CHECK(hex(encryptFigureString("id", 1)) == "9aae06cea539b74e6918d11b16998321");
    CHECK(hex(encryptFigureNumber(1000008, 1)) == "9cbeebf6c190415450766bd771c71589");
    CHECK(hex(encryptFigureString("keysum", 1)) == "1e3650b0e1b48e466dd1de6597aa59ca");
    CHECK(hex(encryptFigureNumber(643481874, 1)) == "c7022255901385ece270ae90cb204402");
    CHECK(hex(encryptFigureString("name", 1)) == "75d3693219a180ec3b4fb38da8147699");
    CHECK(hex(encryptFigureString("AV_Buzz", 1)) == "b247fc2c9f027c298a9bef426944c115");
    CHECK(hex(encryptFigureString("playset", 1)) == "82b7be1543df6fe5ff0ed0c2ca4b9da4");
    CHECK(hex(encryptFigureString("ToyStoryInSpace", 1)) == "293d68dee998da248074bb38df7c7afacf12bdbe2f8f97b8d7e3a0b1ad283116");
    CHECK(hex(encryptFigureString("displayName", 1)) == "c9216724403e8d333ce5bbad3cd8bb4d");
    CHECK(hex(encryptFigureString("IGP_Buzz", 1)) == "7d1ddbf82e45849f59521cec1b7d36cf");
    CHECK(hex(encryptFigureString("id", 2)) == "7fb75a6542b2b9ed39e4321bef6aa184");
    CHECK(hex(encryptFigureNumber(1000016, 2)) == "126b736aedcc518d078acb7734883379");
    CHECK(hex(encryptFigureString("displayName", 38)) == "6e5e58ec1de13ec26f7f03aba0e6f0cc");
    CHECK(hex(encryptFigureString("IGP_BuzzGlow", 38)) == "3be11edf440b867ae6d704d0a02d4de2");
    CHECK(hex(encryptFigureString("parentId", 38)) == "ce7ebe6119c2bf2f31cd9ba568c133fa");
    CHECK(hex(encryptFigureNumber(1000008, 38)) == "b310e90d2cbc2272537567ba665664c0");
    CHECK(hex(encryptFigureString("id", 38)) == "096caeac04b52f0b3d65e4e9539e7d08");
    CHECK(hex(encryptFigureNumber(1000038, 38)) == "8d77cd94d370a5f9fe5a501d53b96ccd");
    CHECK(hex(encryptFigureString("name", 38)) == "50f8ad6fc4cc9f7d35799579a94a8de4");
    CHECK(hex(encryptFigureString("AV_Buzz_Glow", 38)) == "4333535c84c4eb0d1fe0ac9f260b87ad");
}

static CustomFigure zurg() {
    return {1000100, "Zurg", "TS_Zurg", "TS_Zurg", "AV_Buzz", "Emperor Zurg", "", "", "RumpusRoom"};
}

static std::string manifest(const CustomFigure& figure) {
    return "[Mod]\nName=Character test\n[Character]\nSkuId=" + std::to_string(figure.sku) +
        "\nName=" + figure.name + "\nGateway=" + figure.gateway + "\nActor=" + figure.actor +
        "\nBaseCharacter=" + figure.baseCharacter + "\nDisplayName=" + figure.displayName +
        "\nDescription=" + figure.description + "\nIcon=" + figure.icon + "\nPlayset=" + figure.playset + "\n";
}

static void write(const fs::path& file, std::string_view content) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(content.data(), static_cast<std::streamsize>(content.size()));
}

static bool logged(const std::vector<std::string>& messages, std::string_view text) {
    for (const auto& message : messages) if (message.find(text) != std::string::npos) return true;
    return false;
}

static void manifestPolicy() {
    const auto root = fs::temp_directory_path() / (L"literalcrab-figure-tests-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
    unsigned serial{};
    auto mod = [&](std::string_view content) {
        crabe::mods::Mod result;
        result.folder = "figure" + std::to_string(++serial);
        result.name = result.folder;
        const auto path = root / result.folder;
        result.path = path.wstring();
        write(path / "mod.ini", content);
        return result;
    };
    std::vector<std::string> messages;
    const auto validMod = mod(manifest(zurg()));
    auto figures = loadCustomFigures({validMod}, messages);
    CHECK(figures.size() == 1 && figures[0].sku == 1000100 && figures[0].name == "Zurg");
    CHECK(figures.size() == 1 && figures[0].displayName == "Emperor Zurg" && figures[0].baseCharacter == "AV_Buzz");
    const auto gameplay = "\n[Character.Gameplay]\nSourceActor=AV_Zurg\nSourceDNA=characters/TS_Zurg.dnax\nItem=ITEM_ZURG_BOSS_BLASTER\nPreloadActors=tool_zurgbossblaster\nPreloadFiles=Entity|weapons/laser_blast,Cho|choreographies/ts_zurgball/ts_zurgball\n";
    auto privateFigure=zurg();privateFigure.actor=privateFigure.gateway="LCR_Zurg";
    CHECK(loadCustomFigures({mod(manifest(zurg())+gameplay)},messages).empty());
    figures = loadCustomFigures({mod(manifest(privateFigure) + gameplay)}, messages);
    CHECK(figures.size()==1 && figures[0].gameplay.item=="ITEM_ZURG_BOSS_BLASTER" && figures[0].gameplay.preloadFiles.size()==2);
    // Minimal manifests: identity and source; everything else has defaults.
    figures = loadCustomFigures({mod("[Character]\nName=LiteralCrab_Butch\nSource=butch\n")}, messages);
    CHECK(figures.size() == 1);
    if (!figures.empty()) {
        const auto& f = figures[0];
        CHECK(f.sku == defaultFigureSku("LiteralCrab_Butch") && f.sku >= 1000100 && f.sku <= 1999999);
        CHECK(f.gateway == "LCR_LiteralCrab_Butch" && f.actor == f.gateway && f.displayName == f.name);
        CHECK(f.icon == "ui/charactericons/blankavatar" && f.baseCharacter.empty() && f.gameplay.sourceActor == "butch");
        CHECK(f.gameplay.enabled() && f.gameplay.sourceDna.empty() && f.gameplay.body == BodyMode::Auto);
    }
    figures = loadCustomFigures({mod("[Character]\nName=LCR_Ferb\nSource=characters/PNF_Ferb.dnax\nTemplate=PNF_Phineas\nAnimations=template\n")}, messages);
    CHECK(figures.size() == 1 && figures[0].gateway == "LCR_Ferb" && figures[0].gameplay.sourceDna == "characters/PNF_Ferb.dnax" &&
        figures[0].gameplay.templateFigure == "PNF_Phineas" && figures[0].gameplay.animations == AnimationMode::Template);
    figures = loadCustomFigures({mod("[Character]\nName=PortalOnly\nGateway=LCR_PortalOnly\n")}, messages);
    CHECK(figures.size() == 1 && figures[0].baseCharacter == "AV_Buzz" && !figures[0].gameplay.enabled());
    for (const char* bad : {"Template=AV_McQueen\n", "Body=npc\n", "Animations=all\n", "DNA=../outside.lua\n", "DNA=missing.lua\n"})
        CHECK(loadCustomFigures({mod(std::string("[Character]\nName=Bad\nSource=butch\n") + bad)}, messages).empty());
    CHECK(loadCustomFigures({mod("[Character]\nName=Both\nSource=butch\n[Character.Gameplay]\nSourceActor=butch\n")}, messages).empty());
    CHECK(loadCustomFigures({mod("[Character]\nName=NoSource\nTemplate=AV_Buzz\n")}, messages).empty());
    // A data-only Lua overlay is validated at startup, before any registration.
    auto overlay = mod("[Character]\nName=Overlay\nSource=butch\nDNA=butch.lua\n");
    write(fs::path(overlay.path) / "butch.lua", "DNA = { AgentGadgets = { Health = { health = 600 } } }\n");
    figures = loadCustomFigures({overlay}, messages);
    CHECK(figures.size() == 1 && figures[0].gameplay.overlayPatch.find("health") != std::string::npos);
    write(fs::path(overlay.path) / "butch.lua", "DNA = { AgentGadgets = { Health = { health = os.time() } } }\n");
    messages.clear();
    CHECK(loadCustomFigures({overlay}, messages).empty() && logged(messages, "data-only"));
    figures = loadCustomFigures({mod(manifest(privateFigure) + gameplay + "ItemControlSource=ITEM_BLASTER\nItemDisplayName=Zurg Blaster\n")}, messages);
    CHECK(figures.size()==1 && figures[0].gameplay.controlSources[0]=="ITEM_BLASTER" && figures[0].gameplay.toolDisplayNames[0]=="Zurg Blaster");
    figures=loadCustomFigures({mod(manifest(privateFigure)+gameplay+"InventorySource=AV_Buzz\nInventoryExclude=ITEM_BUZZ,ABILITY_BUZZ\nProp=PROP_BUZZ\nPreloadIncludes=biped_avatar\n")},messages);
    CHECK(figures.size()==1 && figures[0].gameplay.inventorySource=="AV_Buzz" && figures[0].gameplay.inventoryExclude.size()==2 && figures[0].gameplay.preloadIncludes==std::vector<std::string>{"biped_avatar"});
    for(const auto* invalid:{"InventorySource=bad/path","InventoryExclude=ITEM_BUZZ","InventorySource=AV_Buzz\nInventoryExclude=PROP_NONE","PreloadIncludes=../escape","PreloadIncludes=biped_avatar|Include"})
        CHECK(loadCustomFigures({mod(manifest(privateFigure)+gameplay+invalid+"\n")},messages).empty());
    const auto weapon="ItemWeaponSource=BossShot\nItemToolDNA=characters/BossTool.dnax\nItemToolScript=characters/BossTool.sx\nItemProjectileRadius=0.15\nAimOffset=0,2,0\n";
    figures=loadCustomFigures({mod(manifest(privateFigure)+gameplay+weapon)},messages);
    CHECK(figures.size()==1 && figures[0].gameplay.itemWeapon.radius==0.15 && figures[0].gameplay.aimOffset && (*figures[0].gameplay.aimOffset)[1]==2);
    for (const auto* invalid : {"SourceActor=TS_Zurg", "SourceDNA=../original.dnax", "Item=bad/path", "Unknown=1", "PreloadFiles=Unknown|file"}) {
        auto source=manifest(privateFigure)+gameplay;
        // A duplicate INI field is invalid too; never accept ambiguous author intent.
        source+=invalid; source+='\n';
        CHECK(loadCustomFigures({mod(source)},messages).empty());
    }
    figures=loadCustomFigures({mod(manifest(privateFigure)+"AssetDirectory=character-assets\n"+gameplay)},messages);
    CHECK(figures.size()==1 && figures[0].assetDirectory=="character-assets" && !figures[0].modPath.empty());
    for (const auto* invalid : {"../escape","/absolute","C:/elsewhere","assets","assets/private",".cache","folder/"})
        CHECK(loadCustomFigures({mod(manifest(privateFigure)+"AssetDirectory="+invalid+"\n"+gameplay)},messages).empty());
    auto actorConflict=privateFigure;actorConflict.actor=actorConflict.gateway="lcr_1000100_item";
    CHECK(loadCustomFigures({mod(manifest(actorConflict)+gameplay+weapon)},messages).empty());
    actorConflict.sku=1000101;actorConflict.name="Other";
    CHECK(loadCustomFigures({mod(manifest(privateFigure)+gameplay+weapon),mod(manifest(actorConflict)+gameplay)},messages).size()==1);
    CHECK(loadCustomFigures({mod(manifest(actorConflict)+gameplay),mod(manifest(privateFigure)+gameplay+weapon)},messages).size()==1);
    const auto defaults = mod("[Character]\nSkuId=1000101\nName=Zurg_Test\nGateway=TS_Zurg\nBaseCharacter=av_buzz\n");
    figures = loadCustomFigures({defaults}, messages);
    CHECK(figures.size() == 1 && figures[0].actor == "TS_Zurg" && figures[0].displayName == "Zurg_Test" &&
        figures[0].playset == "RumpusRoom");
    auto casing = zurg(); casing.actor = "ts_zurg";
    CHECK(loadCustomFigures({mod(manifest(casing))}, messages).size() == 1);
    const auto quoted = mod("[character]\nskuid=\"1000100\"\nname='Zurg'\ngateway=\"TS_Zurg\"\nbasecharacter=AV_Buzz\n");
    CHECK(loadCustomFigures({quoted}, messages).size() == 1);
    CHECK(loadCustomFigures({mod("[Mod]\nName=No character\n")}, messages).empty());

    auto rejected = [&](CustomFigure figure) {
        messages.clear();
        CHECK(loadCustomFigures({mod(manifest(figure))}, messages).empty());
        CHECK(logged(messages, "REJECTED"));
    };
    auto bad = zurg(); bad.sku = 1000099; rejected(bad);
    bad = zurg(); bad.sku = 2000000; rejected(bad);
    bad = zurg(); bad.name = "Emperor Zurg"; rejected(bad);
    bad = zurg(); bad.name = "../Zurg"; rejected(bad);
    bad = zurg(); bad.name = "av_buzz"; rejected(bad);
    // Every portal-only alias from the shipped catalog is reserved independently
    // of the gateway list, including hidden crystal entries and case variants.
    for (const auto* alias : {"AV_MrInc", "Syndrome_name", "Violet_name", "Dash_name", "Woody_name",
        "Jessie_name", "Buzz_name", "mcqueen_name", "Mater_name", "holly_name", "Francesco_name",
        "LoneRanger_name", "Tonto_name", "mickeymouse_name", "BobCrystal_name", "SulleyCrystal_name",
        "JackSparrowCrystal_name", "BuzzCrystal_name", "PerryCrystal_name", "McQueenCrystal_name",
        "LoneRangerCrystal_name", "MickeyMouseCrystal_name", "Buzz_Glow_name"}) {
        bad = zurg(); bad.name = alias; rejected(bad);
        bad = zurg(); bad.gateway = bad.actor = alias; rejected(bad);
    }
    bad = zurg(); bad.name = "SYNDROME_NAME"; rejected(bad);
    bad = zurg(); bad.gateway = bad.actor = "av_mrinc"; rejected(bad);
    bad = zurg(); bad.gateway = "av_buzz"; bad.actor = bad.gateway; rejected(bad);
    bad = zurg(); bad.gateway = "TS/Zurg"; bad.actor = bad.gateway; rejected(bad);
    bad = zurg(); bad.actor = "TS_Other"; rejected(bad);
    bad = zurg(); bad.baseCharacter = "TS_Zurg"; rejected(bad);
    bad = zurg(); bad.baseCharacter = "Buzz_name"; rejected(bad);
    bad = zurg(); bad.playset = "../ToyStory"; rejected(bad);
    bad = zurg(); bad.displayName.assign(256, 'x'); rejected(bad);
    bad = zurg(); bad.description.assign(1025, 'x'); rejected(bad);
    bad = zurg(); bad.icon.assign(256, 'x'); rejected(bad);
    messages.clear();
    CHECK(loadCustomFigures({mod("[Character]\nSkuId=1e6\nName=Zurg\nGateway=TS_Zurg\nBaseCharacter=AV_Buzz\n")}, messages).empty());
    CHECK(loadCustomFigures({mod(manifest(zurg()) + "Name=Other\n")}, messages).empty());

    auto duplicateSku = zurg(); duplicateSku.name = "Other"; duplicateSku.gateway = duplicateSku.actor = "Other";
    auto duplicateName = zurg(); duplicateName.sku++; duplicateName.name = "zurg"; duplicateName.gateway = duplicateName.actor = "Other";
    auto duplicateGateway = zurg(); duplicateGateway.sku += 2; duplicateGateway.name = "Other";
    duplicateGateway.gateway = duplicateGateway.actor = "ts_zurg";
    messages.clear();
    figures = loadCustomFigures({validMod, mod(manifest(duplicateSku)), mod(manifest(duplicateName)), mod(manifest(duplicateGateway))}, messages);
    CHECK(figures.size() == 1 && figures[0].name == "Zurg");
    CHECK(logged(messages, "duplicate custom SkuId, Name or Gateway"));
    // Invalid manifests reserve nothing, so the later valid owner still loads.
    bad = zurg(); bad.actor = "Wrong";
    figures = loadCustomFigures({mod(manifest(bad)), validMod}, messages);
    CHECK(figures.size() == 1 && figures[0].sku == zurg().sku);

    // user.ini has no authority to change identity. The existing mod index decides enablement.
    const auto enabledRoot = root / "enabled";
    write(enabledRoot / "zurg/mod.ini", manifest(zurg()));
    write(enabledRoot / "zurg/user.ini", "[Character]\nSkuId=1999999\nName=Other\n");
    auto index = crabe::mods::Index::load(enabledRoot.wstring(), messages);
    figures = loadCustomFigures(index.mods, messages);
    CHECK(figures.size() == 1 && figures[0].sku == zurg().sku && figures[0].name == "Zurg");
    write(enabledRoot / "zurg/user.ini", "[Mod]\nEnabled=0\n");
    index = crabe::mods::Index::load(enabledRoot.wstring(), messages);
    CHECK(loadCustomFigures(index.mods, messages).empty());

    std::vector<crabe::mods::Mod> many;
    for (unsigned i = 0; i < 65; ++i) {
        auto figure = zurg(); figure.sku += i;
        figure.name = figure.gateway = figure.actor = "Custom_" + std::to_string(i);
        many.push_back(mod(manifest(figure)));
    }
    messages.clear();
    CHECK(loadCustomFigures(many, messages).size() == 64);
    CHECK(logged(messages, "at most 64 custom figures"));
    CHECK(customFigureGatewayPatch({}).empty());
    bool threw{};
    try { customFigureGatewayPatch({zurg(), zurg()}); } catch (const std::exception&) { threw = true; }
    CHECK(threw);
    fs::remove_all(root); // This unique directory and every file in it were created above.
}

static std::string luaLogs;
static int capture(lua_State* state) {
    std::size_t length{};
    if (const auto* message = lua_tolstring(state, 1, &length)) {
        luaLogs.append(message, length); luaLogs += '\n';
    }
    return 0;
}

static bool run(lua_State* state, std::string_view source) {
    if (luaL_loadbuffer(state, source.data(), source.size(), "=fixture") || lua_pcall(state, 0, 0, 0)) {
        std::printf("Lua fixture error: %s\n", lua_tostring(state, -1));
        lua_settop(state, 0); return false;
    }
    return true;
}

static bool patch(lua_State* state, std::string_view source) {
    luaLogs.clear();
    if (luaL_loadbuffer(state, source.data(), source.size(), "=custom_figures")) {
        std::printf("Lua patch compile error: %s\n", lua_tostring(state, -1));
        lua_settop(state, 0); return false;
    }
    lua_pushnil(state); lua_pushnil(state); lua_pushcfunction(state, capture); lua_getglobal(state, "environment");
    if (lua_pcall(state, 4, 0, 0)) {
        std::printf("Lua patch error: %s\n", lua_tostring(state, -1));
        lua_settop(state, 0); return false;
    }
    return true;
}

static bool checkLua(lua_State* state, std::string_view expression) {
    const auto source = "return " + std::string(expression);
    if (luaL_loadbuffer(state, source.data(), source.size(), "=check") || lua_pcall(state, 0, 1, 0)) {
        std::printf("Lua check error: %s\n", lua_tostring(state, -1));
        lua_settop(state, 0); return false;
    }
    const bool result = lua_toboolean(state, -1) != 0;
    lua_settop(state, 0); return result;
}

static void setup(lua_State* state, unsigned rows = 38) {
    CHECK(run(state, "environment = {}; c = {}; original = {}; for i = 1," + std::to_string(rows) +
        " do c[i] = {sentinel='stock'..i}; original[i] = c[i] end; environment[" +
        crabe::mods::luaQuote(customFigureGatewayMarker()) + "] = c"));
}

static std::string rowSource(const CustomFigure& figure, unsigned row) {
    const auto quote = crabe::mods::luaQuote;
    return "{[" + quote(encryptFigureString("id", row)) + "]=" + quote(encryptFigureNumber(figure.sku, row)) +
        ",[" + quote(encryptFigureString("name", row)) + "]=" + quote(encryptFigureString(figure.gateway, row)) +
        ",[" + quote(encryptFigureString("displayName", row)) + "]=" + quote(encryptFigureString(figure.displayName, row)) +
        ",[" + quote(encryptFigureString("playset", row)) + "]=" + quote(encryptFigureString(figure.playset, row)) + "}";
}

static void luaGateway() {
    auto* state = luaL_newstate();
    CHECK(state != nullptr);
    if (!state) return;
    luaL_openlibs(state);
    const auto source = customFigureGatewayPatch({zurg()});
    const std::string stockIntact = "(function() for i=1,38 do if c[i] ~= original[i] or c[i].sentinel ~= 'stock'..i then return false end; "
        "local n=0; for _ in pairs(c[i]) do n=n+1 end; if n ~= 1 then return false end end return true end)()";
    setup(state);
    CHECK(patch(state, source));
    CHECK(checkLua(state, "#c == 39"));
    CHECK(checkLua(state, stockIntact));
    CHECK(luaLogs.find("APPLIED: custom figure Zurg") != std::string::npos);
    CHECK(run(state, "expected = " + rowSource(zurg(), 39)));
    CHECK(checkLua(state, "(function() local n=0; for k,v in pairs(c[39]) do n=n+1; if expected[k] ~= v then return false end end return n==4 end)()"));
    CHECK(patch(state, source));
    CHECK(checkLua(state, "#c == 39"));
    CHECK(checkLua(state, stockIntact));
    CHECK(luaLogs.find("UNCHANGED: custom figure Zurg") != std::string::npos);

    // Host readiness is reset before every attempt and published only when the
    // encrypted native row is verified. A stale success must never survive a skip.
    CHECK(run(state, "ready={}; resets=0; marks=0; LiteralCrab={}; "
        "function LiteralCrab.ResetCustomFigures() resets=resets+1; ready={} end; "
        "function LiteralCrab.MarkCustomFigureReady(sku) marks=marks+1; ready[sku]=true end"));
    setup(state);
    CHECK(patch(state, source));
    CHECK(checkLua(state, "resets==1 and marks==1 and ready[1000100]==true and #c==39"));
    CHECK(run(state, "ready[1999999]=true"));
    CHECK(patch(state, source));
    CHECK(checkLua(state, "resets==2 and marks==2 and ready[1000100]==true and ready[1999999]==nil and #c==39"));

    // Reset is required even before an unsupported array returns early.
    setup(state);
    CHECK(run(state, "c.metadata={}; ready[1000100]=true; marks=0; resets=0"));
    CHECK(patch(state, source));
    CHECK(checkLua(state, "resets==1 and marks==0 and next(ready)==nil and c[39]==nil"));

    auto other = zurg(); other.sku++; other.name = other.gateway = other.actor = "Other";
    setup(state);
    const auto two = customFigureGatewayPatch({zurg(), other});
    CHECK(patch(state, two));
    CHECK(checkLua(state, "#c == 40"));
    CHECK(checkLua(state, "ready[1000100]==true and ready[1000101]==true"));
    CHECK(run(state, "expected = " + rowSource(other, 40)));
    CHECK(checkLua(state, "(function() for k,v in pairs(expected) do if c[40][k] ~= v then return false end end return true end)()"));
    CHECK(patch(state, two));
    CHECK(checkLua(state, "#c == 40"));

    // A collision in either native identity field leaves that pre-existing row intact.
    for (bool sameSku : {true, false}) {
        setup(state);
        auto collision = zurg();
        if (sameSku) collision.gateway = collision.actor = "Other";
        else collision.sku++;
        CHECK(run(state, "c[39] = " + rowSource(collision, 39) + "; saved = c[39]"));
        CHECK(run(state, "ready[1000100]=true; resets=0; marks=0"));
        CHECK(patch(state, source));
        CHECK(checkLua(state, "#c == 39 and c[39] == saved"));
        CHECK(checkLua(state, "resets==1 and marks==0 and next(ready)==nil"));
        CHECK(luaLogs.find("SKIPPED: conflicting custom figure Zurg") != std::string::npos);
        CHECK(checkLua(state, stockIntact));
    }
    // Even two individually matching rows are an ambiguous identity and cannot be extended.
    setup(state);
    CHECK(run(state, "c[39] = " + rowSource(zurg(), 39) + "; c[40] = " + rowSource(zurg(), 40)));
    CHECK(patch(state, source));
    CHECK(checkLua(state, "#c == 40"));
    CHECK(checkLua(state, "next(ready)==nil"));
    CHECK(luaLogs.find("SKIPPED: conflicting custom figure Zurg") != std::string::npos);

    for (const auto* change : {"c[0] = {}", "c.metadata = {}", "c[37] = nil", "c[5] = false"}) {
        setup(state);
        CHECK(run(state, change));
        CHECK(patch(state, source));
        CHECK(checkLua(state, "c[39] == nil"));
        CHECK(checkLua(state, "next(ready)==nil"));
        CHECK(luaLogs.find("SKIPPED:") != std::string::npos);
    }
    for (unsigned count : {37u, 102u, 103u}) {
        setup(state, count);
        CHECK(patch(state, source));
        CHECK(checkLua(state, "#c == " + std::to_string(count)));
        CHECK(checkLua(state, "next(ready)==nil"));
        CHECK(luaLogs.find("SKIPPED:") != std::string::npos);
    }
    setup(state);
    CHECK(run(state, "environment = {}"));
    CHECK(run(state, "ready[1000100]=true; resets=0; marks=0"));
    CHECK(patch(state, source));
    CHECK(checkLua(state, "#c == 38"));
    CHECK(luaLogs.find("Category1 table missing") != std::string::npos);
    CHECK(checkLua(state, "resets==1 and marks==0 and next(ready)==nil"));
    CHECK(checkLua(state, stockIntact));
    lua_close(state);
}

int main() {
    cryptoVectors();
    manifestPolicy();
    luaGateway();
    if (failures) return 1;
    std::puts("PASS: custom figure manifests, original DI1 ciphertexts and Lua gateway registration");
    return 0;
}
