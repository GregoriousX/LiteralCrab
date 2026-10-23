// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_mods.hpp"
#include "di1/character_assets.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace crabe::di1;
namespace fs = std::filesystem;
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static CustomFigure figure() {
    return {1000900, "LiteralCrab_Zurg", "AV_Zurg", "av_zurg", "AV_Buzz", "Zurg", "Custom avatar", "ui/icons/source", "RumpusRoom"};
}
static bool run(lua_State* state, const std::string& code) {
    if (luaL_loadbuffer(state, code.data(), code.size(), "=character-test") || lua_pcall(state, 0, 0, 0)) {
        std::printf("LUA: %s\n", lua_tostring(state, -1)); lua_pop(state, 1); return false;
    }
    return true;
}
static std::string patchCall(const std::string& patch, const char* result) {
    return "(function(...)\n" + patch + "\nend)(" + result + ",{},function() end,_G)";
}
static void generated(crabe::mods::AssetIndex& assets, const char* name, std::string value) {
    crabe::mods::AssetMember member;
    member.name = name; member.generated = std::make_shared<const std::string>(std::move(value));
    assets.members.push_back(std::move(member));
}
static void adapters(lua_State* state, CustomFigure f) {
    f.gameplay.item="ITEM_ZURG_BOSS_BLASTER";f.gameplay.toolDisplayNames[0]="Zurg Blaster";
    setCustomCharacterLabels({f});
    CHECK(std::string(customCharacterText("ITEM_ZURG_BOSS_BLASTER_name"))=="Zurg Blaster");
    crabe::mods::LuaApi api;
    api.type = &lua_type; api.toNumber = &lua_tonumber; api.pushBoolean = &lua_pushboolean;
    addCustomCharacterAdapters(api);
    CHECK(api.adapters.size() == 4);
    lua_getglobal(state, "LiteralCrab");
    for (const auto& entry : api.adapters) {
        lua_pushcfunction(state, entry.function);
        lua_setfield(state, -2, entry.name.c_str());
    }
    lua_pop(state, 1);
}
static std::string sourceTexture() {
    std::string bytes(128 + 128 * 128 * 4, '\0');
    const auto put = [&](unsigned at, std::uint32_t value) { std::memcpy(bytes.data() + at, &value, 4); };
    std::memcpy(bytes.data(), "DDS ", 4); put(4, 124); put(8, 0x100f);
    put(12, 128); put(16, 128); put(20, 512);
    put(76, 32); put(80, 0x41); put(88, 32);
    put(92, 0xff0000); put(96, 0xff00); put(100, 0xff); put(104, 0xff000000); put(108, 0x1000);
    for (unsigned y = 2; y < 128; ++y) for (unsigned x = 26; x < 107; ++x) {
        const auto p = 128 + (y * 128 + x) * 4;
        bytes[p] = 60; bytes[p + 1] = 20; bytes[p + 2] = static_cast<char>(220); bytes[p + 3] = static_cast<char>(255);
    }
    return bytes;
}
static void portraits() {
    auto f = figure();
    std::vector<CustomFigure> figures{f};
    crabe::mods::AssetIndex assets;
    // Minimal independently constructed one-texture fixture, not a game asset.
    std::string bundle(64, '\0');
    const auto put = [&](unsigned at, unsigned value) { std::memcpy(bundle.data() + at, &value, 4); };
    std::memcpy(bundle.data(), "BNDL", 4); put(4, 1); put(8, 64); put(12, 1);
    std::memcpy(bundle.data() + 16, "TEXB", 4); put(20, 3); put(24, 48); put(28, 1); put(32, 7);
    std::memcpy(bundle.data() + 36, "source", 7);
    std::memcpy(bundle.data() + 44, "12345678", 8);
    const auto source = sourceTexture();
    generated(assets, "textures/3132333435363738.tbody", source);
    generated(assets, "ui/icons/source.mtb", bundle);
    std::vector<std::string> messages;
    prepareCustomCharacterAssets(assets, L"nonexistent-originals", figures, messages);
    CHECK(figures.size() == 1 && assets.members.size() == 8);
    for (const char* suffix : {"", "_s", "_sm"}) {
        const auto path = std::string("ui/virtualreadercharacters/literalcrab_zurg") + suffix + ".mtb";
        const auto* member = assets.member(path);
        std::string bytes;
        CHECK(member && assets.read(*member, bytes));
        CHECK(bytes.find(std::string("literalcrab_zurg") + suffix + '\0') != std::string::npos);
        CHECK(bytes.size() % 16 == 0);
        // The shipped TEXB v3 parser reads hash+nameOffset (12 bytes), then
        // dereferences stringTable+nameOffset. The initial implementation wrote
        // only the hash and crashed for this exact 17-byte name table.
        std::uint32_t nameLength{}, nameOffset{};
        std::memcpy(&nameLength, bytes.data() + 32, 4);
        const auto entry = (36u + nameLength + 3u) & ~3u;
        CHECK(entry + 12 <= bytes.size());
        if (entry + 12 <= bytes.size()) {
            std::memcpy(&nameOffset, bytes.data() + entry + 8, 4);
            CHECK(nameOffset < nameLength && nameOffset == 0);
            CHECK(bytes[36 + nameLength - 1] == '\0');
            constexpr char hex[] = "0123456789abcdef";
            std::string texture = "textures/";
            for (unsigned at = entry; at < entry + 8; ++at) {
                const auto ch = static_cast<unsigned char>(bytes[at]);
                texture += hex[ch >> 4]; texture += hex[ch & 15];
            }
            texture += ".tbody";
            const auto* generatedTexture = assets.member(texture);
            std::string canvas;
            CHECK(generatedTexture && assets.read(*generatedTexture, canvas));
            const auto size = std::string_view(suffix) == "_sm" ? 256u : 512u;
            CHECK(canvas.size() == 128u + size * size * 4);
            if (canvas.size() >= 128) {
                std::uint32_t width{}, height{};
                std::memcpy(&width, canvas.data() + 16, 4); std::memcpy(&height, canvas.data() + 12, 4);
                CHECK(canvas.compare(0, 4, "DDS ") == 0 && width == size && height == size);
                unsigned top = size, bottom = 0;
                for (unsigned y = 0; y < size; ++y) for (unsigned x = 0; x < size; ++x)
                    if (static_cast<unsigned char>(canvas[128 + (y * size + x) * 4 + 3]) > 8) {
                        if (y < top) top = y;
                        if (y + 1 > bottom) bottom = y + 1;
                    }
                CHECK(top >= size / 5 && bottom <= size && bottom > top);
                CHECK(bottom - top < size * 4 / 5); // Original NPC icon fills 126/128 of the frame.
            }
        }
    }
    std::string unchanged;
    CHECK(assets.read(*assets.member("textures/3132333435363738.tbody"), unchanged) && unchanged == source);
    const auto count = assets.members.size();
    prepareCustomCharacterAssets(assets, L"nonexistent-originals", figures, messages);
    CHECK(assets.members.size() == count);
    auto broken = figure(); broken.name = "Rejected"; broken.icon = "../../other";
    figures = {broken}; prepareCustomCharacterAssets(assets, L"nonexistent-originals", figures, messages);
    CHECK(figures.empty() && assets.members.size() == count);
    setCustomCharacterLabels({f});
    CHECK(std::string(customCharacterText("@LiteralCrab_Zurg")) == "Zurg");
    CHECK(std::string(customCharacterText("LiteralCrab_Zurg_description")) == "Custom avatar");
    CHECK(!customCharacterText("AV_Buzz") && !customCharacterText(nullptr));
}
static void portal() {
    auto* state = luaL_newstate(); luaL_openlibs(state);
    auto f = figure(); f.baseCharacter = "av_buzz";
    CHECK(run(state, "VirtualReaderData={SourceAvatarData={{Name='Buzz_name',Gateway='AV_Buzz',sku_id='1000008',Category='toy',PCSKU='stock',Item1='stock'}}}; original=VirtualReaderData.SourceAvatarData[1]"));
    CHECK(run(state, patchCall(customCharacterCatalogPatch({f}), "VirtualReaderData")));
    CHECK(run(state, "assert(#VirtualReaderData.SourceAvatarData==2 and original==VirtualReaderData.SourceAvatarData[1]); local r=VirtualReaderData.SourceAvatarData[2]; assert(r.Name=='LiteralCrab_Zurg' and r.Gateway=='AV_Zurg' and r.sku_id=='1000900'); assert(r.PCSKU=='' and r.Item1=='' and r.Description=='LiteralCrab_Zurg_description'); assert(original.PCSKU=='stock')"));
    CHECK(run(state, patchCall(customCharacterCatalogPatch({f}), "VirtualReaderData")));
    CHECK(run(state, "assert(#VirtualReaderData.SourceAvatarData==2); registered=false; worldValid=true; LiteralCrab={has=function()return true end,TagCategory=function(id) assert(id==1000900); return registered and 0 or -1 end}; VirtualReader={IsAvatarLocked=function()return 'stock-lock' end,CharacterLocked=function()return 'stock-lock' end,IsIGPValid=function()return worldValid end}"));
    adapters(state, f);
    CHECK(run(state, "assert(not LiteralCrab.IsCustomFigureReady(1000900)); LiteralCrab.MarkCustomFigureReady(1000901); LiteralCrab.MarkCustomFigureReady('1000900'); LiteralCrab.MarkCustomFigureReady(0/0); assert(not LiteralCrab.IsCustomFigureReady(1000900)); assert(not LiteralCrab.IsCustomFigureReady(nil)); assert(not LiteralCrab.IsCustomFigureReady(1000901))"));
    CHECK(run(state, patchCall(customCharacterPortalPatch({f}), "VirtualReader")));
    CHECK(run(state, "assert(VirtualReader:IsAvatarLocked('AV_Zurg')==true); assert(VirtualReader:CharacterLocked('LiteralCrab_Zurg')==true); assert(not VirtualReader:IsIGPValid('LiteralCrab_Zurg')); assert(VirtualReader:IsAvatarLocked('AV_Buzz')=='stock-lock'); registered=true; assert(VirtualReader:IsAvatarLocked('AV_Zurg')); assert(not VirtualReader:IsIGPValid('LiteralCrab_Zurg')); LiteralCrab.MarkCustomFigureReady(1000900); assert(VirtualReader:IsAvatarLocked('AV_Zurg')==false); assert(VirtualReader:CharacterLocked('LiteralCrab_Zurg')==false); assert(VirtualReader:IsIGPValid('LiteralCrab_Zurg')); worldValid=false; assert(not VirtualReader:IsIGPValid('LiteralCrab_Zurg')); LiteralCrab.ResetCustomFigures(); assert(VirtualReader:IsAvatarLocked('AV_Zurg'))"));
    CHECK(run(state, patchCall(customCharacterPortalPatch({f}), "VirtualReader")));
    CHECK(run(state, "assert(VirtualReader:IsAvatarLocked('AV_Buzz')=='stock-lock')"));
    CHECK(run(state, "LiteralCrab.MarkCustomFigureReady(1000900); assert(LiteralCrab.IsCustomFigureReady(1000900)); LiteralCrab.DisableCustomFigure(1000901); assert(LiteralCrab.IsCustomFigureReady(1000900)); LiteralCrab.DisableCustomFigure('1000900'); assert(LiteralCrab.IsCustomFigureReady(1000900)); LiteralCrab.DisableCustomFigure(1000900); assert(not LiteralCrab.IsCustomFigureReady(1000900)); LiteralCrab.ResetCustomFigures(); LiteralCrab.MarkCustomFigureReady(1000900); assert(not LiteralCrab.IsCustomFigureReady(1000900)); assert(VirtualReader:IsAvatarLocked('AV_Zurg'))"));
    lua_close(state);
}
static void write(const fs::path& file, const std::string& value) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(value.data(), static_cast<std::streamsize>(value.size()));
}
int main(int argc, char** argv) {
    portraits(); portal();
    if (argc == 4) {
        std::vector<std::string> messages;
        auto index = crabe::mods::Index::load(fs::path(argv[2]).wstring(), messages);
        auto figures = loadCustomFigures(index.mods, messages);
        crabe::mods::AssetIndex assets;
        prepareCustomAssetDirectories(assets,figures,messages);
        prepareCustomCharacterAssets(assets, fs::path(argv[1]).wstring(), figures, messages);
        CHECK(figures.size() == 1);
        const fs::path output(argv[3]);
        write(output / "gateway-patch.lua", customFigureGatewayPatch(figures));
        write(output / "catalog-patch.lua", customCharacterCatalogPatch(figures));
        write(output / "portal-patch.lua", customCharacterPortalPatch(figures));
        for (const auto& asset : assets.members) {
            std::string bytes; CHECK(assets.read(asset, bytes)); write(output / "assets" / asset.name, bytes);
        }
        for (const auto& message : messages) std::puts(message.c_str());
        std::printf("Original portrait delivery: %zu resources\n", assets.members.size());
    }
    std::printf("Custom character catalog/portal/portrait checks: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
