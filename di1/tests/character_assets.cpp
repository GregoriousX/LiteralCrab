// SPDX-License-Identifier: GPL-3.0-only
// Private custom-character assets: native names stay owned by the original game,
// while mod-local resources and texture cache keys remain isolated per SKU.
#include "di1/character_assets.hpp"
#include "di1/character_gameplay.hpp"
#include "di1/character_model.hpp"
#include "di1/scene_format.hpp"
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}

using namespace crabe::di1;
using crabe::mods::AssetIndex;
namespace fs = std::filesystem;
static unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)

static void write(const fs::path& path, std::string_view data) {
    fs::create_directories(path.parent_path());
    std::ofstream stream(path, std::ios::binary);
    stream.write(data.data(), static_cast<std::streamsize>(data.size()));
    if (!stream) throw std::runtime_error("cannot write test fixture");
}
struct Fixture {
    fs::path root;
    Fixture() {
        root = fs::absolute(fs::temp_directory_path()) /
            ("literalcrab-character-assets-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()));
        if (!fs::create_directory(root)) throw std::runtime_error("test fixture directory already exists");
    }
    ~Fixture() {
        // Only the uniquely created direct child of the explicit temp directory
        // is ours to remove, including any symlink fixture itself, not its target.
        if (root.parent_path() == fs::absolute(fs::temp_directory_path()) &&
            root.filename().wstring().starts_with(L"literalcrab-character-assets-")) {
            std::error_code ignored; fs::remove_all(root, ignored);
        }
    }
};
static CustomFigure figure(const fs::path& mod, unsigned sku = 1000900) {
    CustomFigure f;
    f.sku = sku; f.name = "Private_" + std::to_string(sku);
    f.gateway = f.actor = "lcr_" + std::to_string(sku);
    f.baseCharacter = "AV_Buzz"; f.displayName = "Zurg"; f.playset = "RumpusRoom";
    f.assetDirectory = "character-assets"; f.modPath = mod.wstring();
    return f;
}
static std::string prefix(const CustomFigure& f) {
    return "characters/literalcrab/assets/" + std::to_string(f.sku) + "/";
}
static void generated(AssetIndex& assets, std::string name, std::string bytes) {
    crabe::mods::AssetMember member; member.name = std::move(name);
    member.generated = std::make_shared<const std::string>(std::move(bytes));
    assets.members.push_back(std::move(member));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}
static std::string served(const AssetIndex& assets, std::string_view path) {
    const auto* member = assets.member(path); std::string bytes;
    CHECK(member && assets.read(*member, bytes)); return bytes;
}
static bool logged(const std::vector<std::string>& messages, std::string_view text) {
    return std::any_of(messages.begin(), messages.end(), [&](const auto& entry) { return entry.find(text) != entry.npos; });
}
static std::string compiled(lua_State* state, const std::string& source) {
    if (luaL_loadbuffer(state, source.data(), source.size(), "=original") != 0) {
        std::printf("LUA compile: %s\n", lua_tostring(state, -1)); lua_pop(state, 1); ++failures; return {};
    }
    std::string bytes;
    CHECK(lua_dump(state, [](lua_State*, const void* data, size_t count, void* out) {
        static_cast<std::string*>(out)->append(static_cast<const char*>(data), count); return 0;
    }, &bytes) == 0);
    lua_pop(state, 1); return bytes;
}
static crabe::mods::LuaApi luaApi(lua_State* state) {
    crabe::mods::LuaApi api{};
    api.loadBuffer = &luaL_loadbuffer; api.getTop = &lua_gettop; api.setTop = &lua_settop;
    api.pushValue = &lua_pushvalue; api.insert = &lua_insert; api.pushClosure = &lua_pushcclosure;
    api.pcall = &lua_pcall; api.toLString = &lua_tolstring;
    for (const char* library : {"_G", "table", "string", "math"}) {
        lua_getglobal(state, library);
        const std::string name = std::strcmp(library, "_G") ? std::string(library) + "." : "";
        for (lua_pushnil(state); lua_next(state, -2); lua_pop(state, 1))
            if (lua_type(state, -2) == LUA_TSTRING && lua_iscfunction(state, -1))
                api.library.push_back({name + lua_tostring(state, -2), lua_tocfunction(state, -1)});
        lua_pop(state, 1);
    }
    return api;
}
static void word(std::string& bytes, std::size_t at, unsigned value) { std::memcpy(bytes.data() + at, &value, 4); }
static unsigned word(std::string_view bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) { ++failures; return 0; }
    unsigned value{}; std::memcpy(&value, bytes.data() + at, 4); return value;
}

static const std::string dna =
    "DNA={AgentGadgets={Identification={Name='Zurg',ActorTypeMask='AV_Zurg'},"
    "EntityInstance={ModelName='characters/TS_Zurg/TS_Zurg'},"
    "Nested={Typed='Entity|characters/TS_Zurg/TS_Zurg',"
    "Backslash='characters\\\\TS_Zurg\\\\TS_Zurg',Unrelated='characters/other/model',Number=17}},"
    "Components={ToolSetManager={}}};"
    "DNA.AgentGadgets.Nested.Self=DNA.AgentGadgets.Nested; return 17";

static void executeDna(lua_State* state, const std::string& bytes, const char* name, const std::string& model) {
    const auto status = crabe::mods::loadBuffer(state, bytes.data(), bytes.size(), name);
    CHECK(status == 0);
    if (status) { std::printf("LUA load: %s\n", lua_tostring(state, -1)); lua_settop(state, 0); return; }
    // Reproduce the game's data-only environment, without exposing libraries.
    lua_newtable(state); lua_setfenv(state, -2); lua_pushvalue(state, -1);
    const auto called = lua_pcall(state, 0, 1, 0); CHECK(called == 0);
    if (called) { std::printf("LUA execute: %s\n", lua_tostring(state, -1)); lua_settop(state, 0); return; }
    CHECK(lua_tonumber(state, -1) == 17); lua_pop(state, 1);
    lua_getfenv(state, -1); lua_getfield(state, -1, "DNA"); lua_getfield(state, -1, "AgentGadgets");
    lua_getfield(state, -1, "Identification"); lua_getfield(state, -1, "Name");
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == "Zurg"); lua_pop(state, 1);
    lua_getfield(state, -1, "ActorTypeMask");
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == "AV_Zurg"); lua_pop(state, 2);
    lua_getfield(state, -1, "EntityInstance"); lua_getfield(state, -1, "ModelName");
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == model); lua_pop(state, 2);
    lua_getfield(state, -1, "Nested"); lua_getfield(state, -1, "Typed");
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == "Entity|" + model); lua_pop(state, 1);
    lua_getfield(state, -1, "Backslash");
    const auto expectedBackslash = model.starts_with("characters/literalcrab/assets/") ? model : "characters\\TS_Zurg\\TS_Zurg";
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == expectedBackslash); lua_pop(state, 1);
    lua_getfield(state, -1, "Unrelated");
    CHECK(lua_isstring(state, -1) && std::string(lua_tostring(state, -1)) == "characters/other/model"); lua_pop(state, 1);
    lua_getfield(state, -1, "Number"); CHECK(lua_tonumber(state, -1) == 17); lua_pop(state, 1);
    lua_getfield(state, -1, "Self"); CHECK(lua_rawequal(state, -1, -2)); lua_settop(state, 0);
}

static void isolatedMounts(const fs::path& root) {
    auto* state = luaL_newstate(); luaL_openlibs(state);
    const auto binary = compiled(state, dna);
    auto first = figure(root / "first"), second = figure(root / "second", 1000901);
    write(fs::path(first.modPath) / first.assetDirectory / "characters/ts_zurg.dnax", dna);
    write(fs::path(second.modPath) / second.assetDirectory / "characters/ts_zurg.dnax", binary);
    for (const auto& f : {first, second}) {
        write(fs::path(f.modPath) / f.assetDirectory / "characters/ts_zurg/ts_zurg.bent", f.name);
        // Bare identity aliases must never rename Identification.Name or masks.
        write(fs::path(f.modPath) / f.assetDirectory / "zurg.bin", "not a resource path in DNA");
        write(fs::path(f.modPath) / f.assetDirectory / "av_zurg.bin", "not an actor type declaration");
    }
    AssetIndex assets;
    generated(assets, "characters/ts_zurg.dnax", "original boss DNA");
    generated(assets, "characters/ts_zurg/ts_zurg.bent", "original boss model");
    std::vector<CustomFigure> figures{first, second}; std::vector<std::string> messages;
    prepareCustomAssetDirectories(assets, figures, messages);
    CHECK(figures.size() == 2 && assets.members.size() == 10);
    if (figures.size() != 2) { lua_close(state); return; }
    const auto& a = figures[0]; const auto& b = figures[1];
    const auto modelA = prefix(a) + "characters/ts_zurg/ts_zurg";
    const auto modelB = prefix(b) + "characters/ts_zurg/ts_zurg";
    CHECK(modelA != modelB);
    CHECK(customAssetPath(a, "Entity|characters/TS_Zurg/TS_Zurg") == "Entity|" + modelA);
    CHECK(customAssetPath(b, "characters\\TS_Zurg\\TS_Zurg.bent") == modelB + ".bent");
    CHECK(customAssetPath(a, "characters/ts_zurg/absent") == "characters/ts_zurg/absent");
    CHECK(served(assets, "characters/ts_zurg.dnax") == "original boss DNA");
    CHECK(served(assets, "characters/ts_zurg/ts_zurg.bent") == "original boss model");
    CHECK(served(assets, modelA + ".bent") == first.name);
    CHECK(served(assets, modelB + ".bent") == second.name);
    CHECK(assets.find(modelA+".bent") && assets.find(modelB+".bent") &&
        *assets.find(modelA+".bent")!=*assets.find(modelB+".bent"));
    CHECK(!assets.find("characters/ts_zurg/ts_zurg.bent"));
    const auto textPrivate = served(assets, prefix(a) + "characters/ts_zurg.dnax");
    const auto binaryPrivate = served(assets, prefix(b) + "characters/ts_zurg.dnax");
    CHECK(textPrivate.starts_with("-- LiteralCrabAsset:1000900:characters/ts_zurg.dnax\n"));
    CHECK(binaryPrivate.starts_with(std::string("\x1bLua", 4)) &&
        binaryPrivate.find("LiteralCrabAsset:1000901:characters/ts_zurg.dnax") != binaryPrivate.npos);
    CHECK(binary.find("LiteralCrabAsset:") == binary.npos);
    crabe::mods::Index index; addCustomAssetPatches(index, figures);
    CHECK(crabe::mods::configure(luaApi(state), std::move(index), [](const char*, ...) {}, false));
    executeDna(state, textPrivate, (prefix(a) + "characters/ts_zurg.dnax").c_str(), modelA);
    executeDna(state, binaryPrivate, "", modelB); // Compiled source identity selects only its owner.
    executeDna(state, dna, "characters/ts_zurg.dnax", "characters/TS_Zurg/TS_Zurg");
    executeDna(state, binary, "", "characters/TS_Zurg/TS_Zurg");
    executeDna(state, identifyCustomDna(binary, "LiteralCrabAsset:1999999:characters/ts_zurg.dnax"), "", "characters/TS_Zurg/TS_Zurg");
    CHECK(customAssetDnaPatch(CustomFigure{}).empty());
    lua_close(state);
}

static std::string bundle() {
    // Independently constructed single TEXB v3 fixture with its native 12-byte
    // hash/nameOffset entry. No shipped game content is included in the tests.
    std::string bytes(64, '\0');
    std::memcpy(bytes.data(), "BNDL", 4); word(bytes, 4, 1); word(bytes, 8, 64); word(bytes, 12, 1);
    std::memcpy(bytes.data() + 16, "TEXB", 4); word(bytes, 20, 3); word(bytes, 24, 48);
    word(bytes, 28, 1); word(bytes, 32, 7); std::memcpy(bytes.data() + 36, "source", 7);
    std::memcpy(bytes.data() + 44, "12345678", 8); return bytes;
}
static std::string texturePath(std::string_view bundleBytes) {
    constexpr char digits[] = "0123456789abcdef";
    if (bundleBytes.size() < 56) { ++failures; return {}; }
    std::string path = "textures/";
    for (unsigned at = 44; at < 52; ++at) {
        const auto c = static_cast<unsigned char>(bundleBytes[at]); path += digits[c >> 4]; path += digits[c & 15];
    }
    return path + ".tbody";
}
static std::string materialChunk() {
    // Two materials share one shader. These are grouped arrays, not repeated
    // name/parameters records. The latter layout caused the native null read.
    std::string data(20,'\0');std::memcpy(data.data(),"MATP",4);
    word(data,4,2);word(data,12,2);word(data,16,1);
    data.append("synthetic_shader",17);data.resize((data.size()+3)&~std::size_t(3));
    const auto count=data.size();data.resize(count+4);word(data,count,2);
    data.append("material_A",11);data.append("material_B",11);
    data.resize((data.size()+1)&~std::size_t(1));data.append(4,'\0');
    data.resize((data.size()+3)&~std::size_t(3));
    const auto hashes=data.size();data.resize(hashes+8);
    word(data,hashes,0x12345678);word(data,hashes+4,0x87654321);
    data.resize((data.size()+15)&~std::size_t(15));data.append(32,'\0');
    word(data,8,static_cast<unsigned>(data.size()));return data;
}
static void textures(const fs::path& root) {
    const auto original = bundle(); const std::string body = "synthetic texture payload";
    auto a = figure(root / "textureA"), b = figure(root / "textureB", 1000901);
    for (const auto& f : {a, b}) {
        write(fs::path(f.modPath) / f.assetDirectory / "ui/icons/custom.mtb", original);
        write(fs::path(f.modPath) / f.assetDirectory / "textures/3132333435363738.tbody", body);
    }
    AssetIndex assets; generated(assets, "ui/icons/custom.mtb", original);
    generated(assets, "textures/3132333435363738.tbody", "original global texture");
    std::vector<CustomFigure> figures{a, b}; std::vector<std::string> messages;
    prepareCustomAssetDirectories(assets, figures, messages);
    CHECK(figures.size() == 2 && assets.members.size() == 6);
    if (figures.size() != 2) return;
    const auto privateA = served(assets, prefix(a) + "ui/icons/custom.mtb");
    const auto privateB = served(assets, prefix(b) + "ui/icons/custom.mtb");
    const auto textureA = texturePath(privateA), textureB = texturePath(privateB);
    // Frozen pre-refactor output: figure namespaces keep their original FNV
    // seed, byte order and concatenation, including the numeric SKU text.
    CHECK(textureA == "textures/8f9a054463ca1445.tbody");
    CHECK(textureA != textureB && textureA != "textures/3132333435363738.tbody" && textureB != "textures/3132333435363738.tbody");
    CHECK(served(assets, textureA) == body && served(assets, textureB) == body);
    CHECK(customAssetPath(figures[0], "textures/3132333435363738.tbody") == textureA);
    CHECK(customAssetPath(figures[1], "textures/3132333435363738.tbody") == textureB);
    CHECK(privateA.size() == original.size() && privateB.size() == original.size());
    CHECK(privateA.substr(0, 44) == original.substr(0, 44) && privateA.substr(52) == original.substr(52));
    CHECK(privateB.substr(0, 44) == original.substr(0, 44) && privateB.substr(52) == original.substr(52));
    CHECK(served(assets, "ui/icons/custom.mtb") == original);
    CHECK(served(assets, "textures/3132333435363738.tbody") == "original global texture");
    // Native model MTB: nameless multi-texture TEXB followed by material data.
    // Only hashes change; material offsets and every material byte are retained.
    std::string model(64,'\0');std::memcpy(model.data(),"BNDL",4);
    word(model,4,1);word(model,12,2);
    std::memcpy(model.data()+16,"TEXB",4);word(model,20,3);word(model,24,48);word(model,28,2);
    std::memcpy(model.data()+36,"12345678",8);std::memcpy(model.data()+48,"87654321",8);
    model+=materialChunk();word(model,8,static_cast<unsigned>(model.size()));
    auto m=figure(root/"model-textures",1000902);
    write(fs::path(m.modPath)/m.assetDirectory/"characters/model.mtb",model);
    write(fs::path(m.modPath)/m.assetDirectory/"textures/3132333435363738.tbody",body);
    figures={m};prepareCustomAssetDirectories(assets,figures,messages);CHECK(figures.size()==1);
    if(!figures.empty()) {
        const auto result=served(assets,prefix(m)+"characters/model.mtb");
        CHECK(result.size()==model.size() && result.substr(0,36)==model.substr(0,36));
        CHECK(result.substr(36,8)!=model.substr(36,8) && result.substr(44)==model.substr(44));
    }
    // Shipped bundles may leave a material unnamed (PIR_Tia_Dalma's second one).
    {
        std::string unnamed=materialChunk();
        unnamed.erase(44,10); // "material_A" -> "".
        word(unnamed,8,static_cast<unsigned>(unnamed.size()));
        std::string bundled(64,'\0');std::memcpy(bundled.data(),"BNDL",4);
        word(bundled,4,1);word(bundled,12,2);
        std::memcpy(bundled.data()+16,"TEXB",4);word(bundled,20,3);word(bundled,24,48);word(bundled,28,2);
        std::memcpy(bundled.data()+36,"12345678",8);std::memcpy(bundled.data()+48,"87654321",8);
        bundled+=unnamed;word(bundled,8,static_cast<unsigned>(bundled.size()));
        auto u=figure(root/"unnamed-material",1000904);
        write(fs::path(u.modPath)/u.assetDirectory/"characters/model.mtb",bundled);
        write(fs::path(u.modPath)/u.assetDirectory/"textures/3132333435363738.tbody",body);
        figures={u};prepareCustomAssetDirectories(assets,figures,messages);CHECK(figures.size()==1);
    }
    word(model,72,21);auto malformed=figure(root/"bad-model-chunk",1000903);
    write(fs::path(malformed.modPath)/malformed.assetDirectory/"model.mtb",model);
    write(fs::path(malformed.modPath)/malformed.assetDirectory/"textures/3132333435363738.tbody",body);
    const auto before=assets.members.size();figures={malformed};prepareCustomAssetDirectories(assets,figures,messages);
    CHECK(figures.empty() && assets.members.size()==before);
}
static void malformedMaterials(const fs::path& root) {
    auto data=materialChunk();
    const auto check=[&](std::string table,unsigned serial) {
        auto model=bundle();model+=table;word(model,12,2);word(model,8,static_cast<unsigned>(model.size()));
        auto f=figure(root/("bad-material-"+std::to_string(serial)),1000950+serial);
        write(fs::path(f.modPath)/f.assetDirectory/"model.mtb",model);
        AssetIndex assets;generated(assets,"untouched","native");
        const auto old=assets.members.size();std::vector<CustomFigure> figures{f};std::vector<std::string> messages;
        prepareCustomAssetDirectories(assets,figures,messages);
        CHECK(figures.empty() && assets.members.size()==old && assets.redirects.empty());
        CHECK(logged(messages,"MATP"));
    };
    // Header strings end at38; material-count word at40, two names at44..65,
    // shader-index array at66. Invalid shader1 of a single-shader bundle would
    // become an unchecked native pointer read, even with no local texture body.
    data[66]=1;check(data,0);
    data=materialChunk();word(data,40,1);check(data,1);
    data=materialChunk();word(data,16,0xffffffff);check(data,2);
    data=materialChunk();data.resize(66);word(data,8,66);check(data,3);
    // Regression for the converter's interleaved name/parameter representation.
    data=materialChunk();data[55]='\x01';check(data,4);
}

static std::string script() {
    std::string bytes(0x7c, '\0'); std::memcpy(bytes.data(), "SLNG\x0c\x10\0\0\x78\x56\x34\x12", 12);
    word(bytes, 0x30, 3); word(bytes, 0x64, 0x70 - 0x65);
    unsigned i{};
    for (const auto* text : {"characters/TS_Zurg/TS_Zurg", "DNA|characters/TS_Zurg.dnax", "not an asset"}) {
        const auto at = 0x70 + 4 * i++; word(bytes, at, static_cast<unsigned>(bytes.size() - at - 1));
        bytes += text; bytes += '\0';
    }
    word(bytes, 12, static_cast<unsigned>(bytes.size())); return bytes;
}
static std::string literal(std::string_view bytes, unsigned index) {
    const auto table = 0x65u + word(bytes, 0x64), entry = table + index * 4;
    const auto start = entry + 1ull + word(bytes, entry);
    const auto end = bytes.find('\0', static_cast<std::size_t>(start));
    if (start >= bytes.size() || end == bytes.npos) { ++failures; return {}; }
    return std::string(bytes.substr(static_cast<std::size_t>(start), end - static_cast<std::size_t>(start)));
}
static void scripts(const fs::path& root) {
    auto f = figure(root / "script"); const auto original = script();
    write(fs::path(f.modPath) / f.assetDirectory / "characters/custom.sx", original);
    write(fs::path(f.modPath) / f.assetDirectory / "characters/ts_zurg.dnax", dna);
    write(fs::path(f.modPath) / f.assetDirectory / "characters/ts_zurg/ts_zurg.bent", "model");
    AssetIndex assets; generated(assets, "characters/custom.sx", original);
    std::vector<CustomFigure> figures{f}; std::vector<std::string> messages;
    prepareCustomAssetDirectories(assets, figures, messages); CHECK(figures.size() == 1);
    const auto result = served(assets, prefix(f) + "characters/custom.sx");
    CHECK(literal(result, 0) == prefix(f) + "characters/ts_zurg/ts_zurg");
    CHECK(literal(result, 1) == "DNA|" + prefix(f) + "characters/ts_zurg.dnax");
    CHECK(literal(result, 2) == "not an asset");
    CHECK(word(result, 12) == result.size() && word(result, 0x30) == 3);
    CHECK(result.substr(16, 0x60) == original.substr(16, 0x60));
    CHECK(served(assets, "characters/custom.sx") == original);
}

static void standaloneMounts(const fs::path& root) {
    auto* state = luaL_newstate(); luaL_openlibs(state);
    const auto binary = compiled(state, dna);
    auto f = figure(root / "shared-private-assets");
    const auto folder = fs::path(f.modPath) / f.assetDirectory;
    const auto secondMod = root / "second-tool-assets";
    const std::string body = "synthetic texture payload";
    const auto originalScript = script(), originalBundle = bundle();
    for (const auto& mount : {folder, secondMod / "tool-assets"}) {
        write(mount / "characters/ts_zurg.dnax", mount == folder ? dna : binary);
        write(mount / "characters/ts_zurg/ts_zurg.bent", "private model");
        write(mount / "characters/custom.sx", originalScript);
        write(mount / "ui/icons/custom.mtb", originalBundle);
        write(mount / "textures/3132333435363738.tbody", body);
    }
    AssetIndex assets;
    generated(assets, "characters/ts_zurg.dnax", "original boss DNA");
    generated(assets, "characters/ts_zurg/ts_zurg.bent", "original boss model");
    generated(assets, "textures/3132333435363738.tbody", "original global texture");
    std::vector<CustomFigure> figures{f}; std::vector<std::string> messages;
    prepareCustomAssetDirectories(assets, figures, messages);
    CHECK(figures.size() == 1);
    if (figures.empty()) { lua_close(state); return; }

    ResourceAliases first, second;
    const std::string firstKey = "tools/lcr_ts3_zurg_blaster", secondKey = "tools/second_blaster";
    const auto firstPrefix = "characters/literalcrab/assets/" + firstKey + "/";
    const auto secondPrefix = "characters/literalcrab/assets/" + secondKey + "/";
    preparePrivateAssetDirectory(assets, f.modPath, f.assetDirectory, firstKey, first);
    preparePrivateAssetDirectory(assets, secondMod.wstring(), "tool-assets", secondKey, second);
    CHECK(assets.members.size() == 18);
    const auto logical = "characters/ts_zurg/ts_zurg";
    CHECK(privateAssetPath(first, "Entity|characters/TS_Zurg/TS_Zurg") == "Entity|" + firstPrefix + logical);
    CHECK(privateAssetPath(second, "characters\\TS_Zurg\\TS_Zurg.bent") == secondPrefix + logical + ".bent");
    CHECK(privateAssetPath(first, "characters/absent") == "characters/absent");
    CHECK(privateAssetPath(ResourceAliases{}, logical) == logical);
    CHECK(privateAssetDnaPatch(ResourceAliases{}).empty());
    CHECK(served(assets, firstPrefix + "characters/ts_zurg.dnax").starts_with(
        "-- LiteralCrabAsset:" + firstKey + ":characters/ts_zurg.dnax\n"));
    const auto privateBinary = served(assets, secondPrefix + "characters/ts_zurg.dnax");
    CHECK(privateBinary.starts_with(std::string("\x1bLua", 4)) &&
        privateBinary.find("LiteralCrabAsset:" + secondKey + ":characters/ts_zurg.dnax") != privateBinary.npos);
    CHECK(binary.find("LiteralCrabAsset:") == binary.npos);
    for (const auto& [aliases, mountPrefix] : {std::pair{first, firstPrefix}, std::pair{second, secondPrefix}}) {
        const auto mappedScript = served(assets, mountPrefix + "characters/custom.sx");
        CHECK(literal(mappedScript, 0) == mountPrefix + logical);
        CHECK(literal(mappedScript, 1) == "DNA|" + mountPrefix + "characters/ts_zurg.dnax");
        CHECK(literal(mappedScript, 2) == "not an asset");
        CHECK(mappedScript.substr(16, 0x60) == originalScript.substr(16, 0x60));
        const auto mappedBundle = served(assets, mountPrefix + "ui/icons/custom.mtb");
        CHECK(mappedBundle.substr(0, 44) == originalBundle.substr(0, 44) &&
            mappedBundle.substr(52) == originalBundle.substr(52));
        const auto texture = texturePath(mappedBundle);
        CHECK(texture == privateAssetPath(aliases, "textures/3132333435363738.tbody"));
        CHECK(served(assets, texture) == body);
        CHECK(assets.find(mountPrefix + logical + ".bent"));
        CHECK(!assets.find(mountPrefix + "characters/ts_zurg.dnax"));
    }
    const auto firstTexture = privateAssetPath(first, "textures/3132333435363738.tbody");
    const auto secondTexture = privateAssetPath(second, "textures/3132333435363738.tbody");
    const auto figureTexture = customAssetPath(figures[0], "textures/3132333435363738.tbody");
    CHECK(firstTexture != secondTexture && firstTexture != figureTexture && secondTexture != figureTexture);
    CHECK(served(assets, "characters/ts_zurg.dnax") == "original boss DNA");
    CHECK(served(assets, "characters/ts_zurg/ts_zurg.bent") == "original boss model");
    CHECK(served(assets, "textures/3132333435363738.tbody") == "original global texture");

    // The generic numeric-key path produces the exact same figure output and
    // disk redirects as the public figure wrapper, including the DNA patch.
    AssetIndex numeric; ResourceAliases numericAliases;
    preparePrivateAssetDirectory(numeric, f.modPath, f.assetDirectory, std::to_string(f.sku), numericAliases);
    CHECK(numericAliases == figures[0].assetAliases);
    CHECK(privateAssetDnaPatch(numericAliases) == customAssetDnaPatch(figures[0]));
    for (const auto& member : numeric.members) CHECK(served(numeric, member.name) == served(assets, member.name));
    for (const auto& redirect : numeric.redirects) {
        const auto* target = assets.find(redirect.path); CHECK(target && *target == redirect.target);
    }

    crabe::mods::Index index; addCustomAssetPatches(index, figures);
    for (const auto& [key, aliases] : {std::pair{firstKey, first}, std::pair{secondKey, second}}) {
        const auto path = privateAssetPath(aliases, "characters/ts_zurg.dnax");
        const auto marker = "LiteralCrabAsset:" + key + ":characters/ts_zurg.dnax";
        index.addBuiltinPatch({path, marker, "contains:" + marker}, key, privateAssetDnaPatch(aliases), "{}");
    }
    CHECK(crabe::mods::configure(luaApi(state), std::move(index), [](const char*, ...) {}, false));
    executeDna(state, served(assets, firstPrefix + "characters/ts_zurg.dnax"), "", firstPrefix + logical);
    executeDna(state, privateBinary, "", secondPrefix + logical);
    executeDna(state, served(assets, prefix(f) + "characters/ts_zurg.dnax"), "", prefix(f) + logical);
    executeDna(state, dna, "characters/ts_zurg.dnax", "characters/TS_Zurg/TS_Zurg");
    lua_close(state);

    // Every failure preserves both index content and the caller's existing
    // alias list, including validation after an earlier resource was prepared.
    const auto previous = assets.members;
    const auto redirects = assets.redirects;
    const auto beforeAliases = first;
    const auto rejectGeneric = [&](std::string_view directory, std::string_view key, std::string_view reason) {
        bool rejected{};
        try { preparePrivateAssetDirectory(assets, f.modPath, directory, key, first); }
        catch (const std::exception& error) { rejected = true; CHECK(std::string(error.what()).find(reason) != std::string::npos); }
        CHECK(rejected && first == beforeAliases && assets.members.size() == previous.size());
        CHECK(assets.redirects.size() == redirects.size());
        for (std::size_t i = 0; i < assets.members.size() && i < previous.size(); ++i)
            CHECK(assets.members[i].name == previous[i].name && assets.members[i].generated == previous[i].generated);
        for (std::size_t i = 0; i < assets.redirects.size() && i < redirects.size(); ++i)
            CHECK(assets.redirects[i].path == redirects[i].path && assets.redirects[i].target == redirects[i].target);
    };
    for (const auto* key : {"", "Tools/lcr_blaster", "tools\\lcr_blaster", "../tools", "tools/./lcr_blaster", ".", "tools/.", "./tools", "tools//a"})
        rejectGeneric(f.assetDirectory, key, "invalid private resource namespace");
    rejectGeneric(f.assetDirectory, firstKey, "already claimed");
    rejectGeneric(f.assetDirectory, std::to_string(f.sku), "already claimed");
    rejectGeneric("../outside", "tools/unpublished", "invalid private asset folder");
    write(fs::path(f.modPath) / "broken/a/model.bent", "valid earlier resource");
    write(fs::path(f.modPath) / "broken/z/invalid.sx", "invalid script");
    rejectGeneric("broken", "tools/unpublished", "unsupported private SLNG");
}

static void reject(AssetIndex& assets, CustomFigure f, std::string_view reason) {
    const auto previous = assets.members;
    const auto redirects = assets.redirects.size();
    std::vector<CustomFigure> figures{std::move(f)}; std::vector<std::string> messages;
    prepareCustomAssetDirectories(assets, figures, messages);
    CHECK(figures.empty() && assets.members.size() == previous.size());
    CHECK(assets.redirects.size()==redirects);
    CHECK(logged(messages, "REJECTED") && logged(messages, reason));
    for (std::size_t i = 0; i < previous.size() && i < assets.members.size(); ++i) {
        CHECK(assets.members[i].name == previous[i].name && assets.members[i].generated == previous[i].generated);
    }
}
static void rejectedMounts(const fs::path& root) {
    AssetIndex assets; generated(assets, "characters/ts_zurg.dnax", "original boss DNA");
    auto missing = figure(root / "missing"); reject(assets, missing, "missing");
    auto empty = figure(root / "empty"); fs::create_directories(fs::path(empty.modPath) / empty.assetDirectory);
    reject(assets, empty, "empty");
    auto traverse = figure(root / "traversal"); traverse.assetDirectory = "../outside";
    write(root / "outside/model.bent", "must not be published"); reject(assets, traverse, "invalid");
    traverse.assetDirectory = "character-assets/../../outside"; reject(assets, traverse, "invalid");
    auto broken = figure(root / "malformed");
    write(fs::path(broken.modPath) / broken.assetDirectory / "a/model.bent", "valid earlier resource");
    write(fs::path(broken.modPath) / broken.assetDirectory / "z/invalid.dnax", std::string("\x1bLua\0", 5));
    reject(assets, broken, "bytecode"); // A later failure must not publish the earlier resource.
    auto conflicting = figure(root / "conflicting");
    write(fs::path(conflicting.modPath) / conflicting.assetDirectory / "a.bent", "private model");
    generated(assets, prefix(conflicting) + "a.bent", "another claimant");
    reject(assets, conflicting, "already claimed");
    auto duplicateHash = figure(root / "duplicate-hash");
    write(fs::path(duplicateHash.modPath) / duplicateHash.assetDirectory / "one/3132333435363738.tbody", "first");
    write(fs::path(duplicateHash.modPath) / duplicateHash.assetDirectory / "two/3132333435363738.tbody", "second");
    reject(assets, duplicateHash, "ambiguous private texture hash");
    auto badBundle = figure(root / "malformed-bundle");
    write(fs::path(badBundle.modPath) / badBundle.assetDirectory / "textures/3132333435363738.tbody", "texture");
    write(fs::path(badBundle.modPath) / badBundle.assetDirectory / "ui/icon.mtb", "not a texture bundle");
    reject(assets, badBundle, "TEXB");
    auto first = figure(root / "duplicate-sku-first", 1000999), second = figure(root / "duplicate-sku-second", 1000999);
    write(fs::path(first.modPath) / first.assetDirectory / "shared.bent", "first owner's model");
    write(fs::path(second.modPath) / second.assetDirectory / "shared.bent", "second owner's model");
    std::vector<CustomFigure> sameSku{first, second}; std::vector<std::string> messages;
    const auto before = assets.members.size(); prepareCustomAssetDirectories(assets, sameSku, messages);
    CHECK(sameSku.size() == 1 && assets.members.size() == before + 1 && logged(messages, "already claimed"));
    CHECK(served(assets, prefix(first) + "shared.bent") == "first owner's model");
    CHECK(served(assets, "characters/ts_zurg.dnax") == "original boss DNA");
}

static void reparsePoints(const fs::path& root) {
    auto f = figure(root / "reparse");
    const auto folder = fs::path(f.modPath) / f.assetDirectory;
    fs::create_directories(folder); write(root / "link-target/model.bent", "outside the mount");
    const auto link = folder / "linked";
    if (!CreateSymbolicLinkW(link.c_str(), (root / "link-target").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2)) {
        std::printf("SKIP optional reparse fixtures: symbolic link creation unavailable (%lu).\n", GetLastError()); return;
    }
    AssetIndex assets; reject(assets, f, "reparse");
    // An intermediate link must be rejected even when the final directory is ordinary.
    const auto bridge = root / "bridge"; fs::create_directories(bridge);
    CHECK(CreateSymbolicLinkW((bridge / "linked").c_str(), (root / "link-target").c_str(), SYMBOLIC_LINK_FLAG_DIRECTORY | 2));
    fs::create_directories(root / "link-target/inner"); write(root / "link-target/inner/model.bent", "nested");
    f.modPath = bridge.wstring(); f.assetDirectory = "linked/inner"; reject(assets, f, "reparse");
}

// Humanoids on non-player trees (mission givers) share the biped joints; a
// rig missing any limb joint (a car, robot or animal) is not driven by template sets.
static void skeletons() {
    ModelInfo humanoid;
    for (const auto* joint : {"Cntrl_Hips", "Cntrl_Head", "Cntrl_Clavicle_Left", "Cntrl_Clavicle_Right", "Cntrl_Elbow_Left",
        "Cntrl_Elbow_Right", "Cntrl_Hand_Left", "Cntrl_Hand_Right", "Cntrl_Knee_Left", "Cntrl_Knee_Right",
        "Cntrl_Ankle_Left", "Cntrl_Ankle_Right", "Cntrl_Jaw_Pivot"}) humanoid.controls.insert(joint);
    CHECK(bipedSkeleton(humanoid));
    auto partial = humanoid; partial.controls.erase("Cntrl_Knee_Right"); CHECK(!bipedSkeleton(partial));
    CHECK(!bipedSkeleton(ModelInfo{}));
}
// A minimal BENT: one channel, a mesh, and groups of File aliases.
static std::string entity(const char* channel, std::initializer_list<std::pair<const char*,
    std::initializer_list<std::pair<const char*, const char*>>>> sets) {
    scene::Scene s; s.strings = {""};
    const auto node = [&](std::uint16_t depth, const char* name) { s.atoms.push_back({depth, 0x01, scene::intern(s, name), {}}); };
    const auto text = [&](std::uint16_t depth, const char* name, const char* value) {
        s.atoms.push_back({depth, 0x0B, scene::intern(s, name), {scene::intern(s, value)}});
    };
    node(0, "Root"); node(1, "Channels"); text(2, "Channel", channel); text(1, "SceneFilename", "hero.oct"); node(1, "FileSets");
    for (const auto& [group, files] : sets) {
        text(2, "FileSet", group); node(3, "Files");
        for (const auto& [alias, file] : files) { text(4, "File", alias); text(5, "Filename", file); }
    }
    return scene::encode(s);
}
// Authored/shipped player entities gain the template's shared interactions
// (missing groups, LCRM_ aliases) without losing or changing their own.
static void interactions() {
    for (const bool sameChannels : {true, false}) {
        AssetIndex assets;
        const std::string entityName = "characters/literalcrab/assets/1000777/characters/hero/hero.bent";
        generated(assets, entityName, entity("layer", {{"Core:BaseMobility", {{"IDLE", "motions/idle.oct"}}},
            {"Core:Attacks", {{"ATTACK", "motions/a.oct"}}}}));
        generated(assets, "characters/tpl/tpl.bent", entity(sameChannels ? "layer" : "other", {
            {"Core:BaseMobility", {{"IDLE", "motions/idle.oct"}}},
            {"Core:Attacks", {{"ATTACK", "motions/b.oct"}, {"LCRM_POSE", "motions/pose.oct"}, {"OTHER", "motions/other.oct"}}},
            {"Mech:Bike", {{"BIKE", "motions/bike.oct"}}}}));
        generated(assets, "characters/tpl/motions/pose.oct", "pose");
        generated(assets, "characters/tpl/motions/bike.oct", "bike");
        assets.redirects.push_back({entityName, L"unused"});
        NativeCatalog catalog(assets, L"");
        ResourceAliases aliases;
        const auto result = preparePlayerModel(catalog, assets, "1000777", "characters/hero/hero", "characters/tpl/tpl",
            AnimationMode::Auto, aliases);
        CHECK(result.channelsDiffer == !sameChannels);
        if (!sameChannels) {
            // Different playback channels: the author's entity stays exactly as supplied.
            CHECK(result.mode == AnimationMode::Source && result.addedGroups.empty() && !result.addedAliases);
            CHECK(assets.redirects.size() == 1 && !assets.member("characters/literalcrab/assets/1000777/characters/tpl/motions/bike.oct"));
            continue;
        }
        CHECK(result.mode == AnimationMode::Complete);
        CHECK(result.addedGroups == std::vector<std::string>{"Mech:Bike"} && result.addedAliases == 1);
        CHECK(assets.redirects.empty() && served(assets, "characters/literalcrab/assets/1000777/characters/tpl/motions/bike.oct") == "bike");
        const auto built = scene::decode(served(assets, entityName));
        std::vector<std::string> attacks;
        for (std::size_t i = 0; i < built.atoms.size(); ++i)
            if (scene::name(built, built.atoms[i]) == "File") attacks.emplace_back(scene::text(built, built.atoms[i]));
        // IDLE and ATTACK keep their own clips; OTHER is a template power, never borrowed.
        CHECK(std::count(attacks.begin(), attacks.end(), "ATTACK") == 1 && std::count(attacks.begin(), attacks.end(), "OTHER") == 0);
        CHECK(std::count(attacks.begin(), attacks.end(), "LCRM_POSE") == 1);
        CHECK(std::count(attacks.begin(), attacks.end(), "BIKE") == 1);
        bool ownClip{};
        for (const auto& atom : built.atoms) ownClip = ownClip || (scene::isText(atom) && scene::text(built, atom) == "motions/a.oct");
        CHECK(ownClip);
    }
}
int main() {
    try {
        skeletons(); interactions();
        Fixture fixture;
        isolatedMounts(fixture.root); textures(fixture.root); malformedMaterials(fixture.root);
        scripts(fixture.root); standaloneMounts(fixture.root);
        rejectedMounts(fixture.root); reparsePoints(fixture.root);
    } catch (const std::exception& error) { std::printf("EXCEPTION: %s\n", error.what()); ++failures; }
    std::printf("Private character asset checks: %u failure(s).\n", failures);
    return failures ? 1 : 0;
}
