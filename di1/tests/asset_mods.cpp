// SPDX-License-Identifier: GPL-3.0-only
// ZIP reading, request-name mapping, on-disk redirects and member reads against a synthetic
// game folder and mods.
#include "mods/asset_mods.hpp"
#include "mods/file_redirect.hpp"
#include "mods/zip.hpp"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

using namespace crabe::mods;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static void write(const fs::path& file, std::string_view data) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}
static std::string slurp(const fs::path& file) {
    std::ifstream input(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(input), {});
}
static bool logged(const std::vector<std::string>& messages, std::string_view text) {
    for (const auto& message : messages) if (message.find(text) != std::string::npos) return true;
    std::printf("  missing message: %.*s\n", static_cast<int>(text.size()), text.data());
    return false;
}

// An ordinary ZIP as common tools write it: local headers first, central directory at the end.
struct Item { std::string name; std::uint16_t method; std::string data; std::string stored; };
static void put16(std::string& out, unsigned value) { out += static_cast<char>(value & 0xFF); out += static_cast<char>((value >> 8) & 0xFF); }
static void put32(std::string& out, std::uint32_t value) { put16(out, value & 0xFFFF); put16(out, value >> 16); }
static std::string normalZip(const std::vector<Item>& items) {
    std::string body, directory;
    for (const auto& item : items) {
        const auto crc = zip::crc32(item.data.data(), item.data.size());
        const auto offset = static_cast<std::uint32_t>(body.size());
        put32(body, 0x04034B50); put16(body, 20); put16(body, 0); put16(body, item.method); put16(body, 0); put16(body, 0x5A21);
        put32(body, crc); put32(body, static_cast<std::uint32_t>(item.stored.size())); put32(body, static_cast<std::uint32_t>(item.data.size()));
        put16(body, static_cast<unsigned>(item.name.size())); put16(body, 4); body += item.name; body += std::string("\xFE\xCA\0\0", 4);
        body += item.stored;
        put32(directory, 0x02014B50); put16(directory, 63); put16(directory, 20); put16(directory, 0); put16(directory, item.method);
        put16(directory, 0); put16(directory, 0x5A21); put32(directory, crc); put32(directory, static_cast<std::uint32_t>(item.stored.size()));
        put32(directory, static_cast<std::uint32_t>(item.data.size())); put16(directory, static_cast<unsigned>(item.name.size()));
        put16(directory, 0); put16(directory, 0); put16(directory, 0); put16(directory, 0); put32(directory, 32); put32(directory, offset);
        directory += item.name;
    }
    std::string end;
    put32(end, 0x06054B50); put16(end, 0); put16(end, 0); put16(end, static_cast<unsigned>(items.size())); put16(end, static_cast<unsigned>(items.size()));
    put32(end, static_cast<std::uint32_t>(directory.size())); put32(end, static_cast<std::uint32_t>(body.size())); put16(end, 0);
    return body + directory + end;
}
static std::string member(const zip::Archive& archive, const char* name) {
    const auto* entry = archive.find(name);
    return entry ? archive.read(*entry) : "<missing>";
}
static std::string served(const AssetIndex& index, std::string_view name) {
    const auto* item = index.member(name);
    std::string data;
    return item && index.read(*item, data) ? data : "<none>";
}

static void addedContentFiles(const fs::path& root) {
    const auto game = root / "audio-game" / "assets";
    const auto mods = root / "audio-mods";
    const auto cache = mods / ".cache";
    write(game / "sound/dialog/ENU/native.fsb", "factory bank");
    write(game / "sound/dialog/ENU/native.name", "factory names");
    write(mods / "00_disabled/mod.ini", "[Mod]\nName=Disabled language\n");
    write(mods / "00_disabled/user.ini", "[Mod]\nEnabled=0\n");
    write(mods / "00_disabled/assets/sound/dialog/JPN/disabled.fsb", "disabled");
    write(mods / "00_disabled/assets/custom/disabled/file.new", "disabled custom file");
    for (const auto* mod : {"10_early", "20_later", "30_pack", "40_damaged"})
        write(mods / mod / "mod.ini", "[Mod]\nName=Audio fixture\n");
    write(mods / "10_early/assets/sound/dialog/PLP/shared.fsb", "early bank");
    write(mods / "10_early/assets/sound/dialog/PLP/shared.name", "early names");
    write(mods / "20_later/assets/sound/dialog/PLP/shared.fsb", "later bank");
    write(mods / "20_later/assets/sound/dialog/PLP/shared.name", "later names");
    write(mods / "20_later/assets/sound/dialog/PLP/unique.fsb", "later unique bank");
    write(mods / "20_later/assets/sound/dialog/PLP/unique.name", "later unique names");
    write(mods / "20_later/assets/sound/dialog/RUR/russian.fsb", "russian bank");
    write(mods / "20_later/assets/sound/dialog/RUR/russian.name", "russian names");
    write(mods / "20_later/assets/sound/dialog/RUR/readme.txt", "not a native bank");
    write(mods / "20_later/assets/other/table.name", "not a sound sidecar");
    write(mods / "10_early/assets/characters/customhero/nested/rig.newformat", "custom rig");
    write(mods / "20_later/assets/characters/customhero/nested/rig.newformat", "lower priority rig");
    write(mods / "20_later/assets/characters/customhero/nested/unique.custom", "custom component");
    std::string bankData;
    for (int n = 0; n < 20; ++n) bankData += "PS3 buttons! ";
    write(mods / "30_pack/audio.zip", normalZip({
        {"assets/sound/dialog/ARS/arabic.fsb", 8, bankData, std::string("\x0b\x08\x36\x56\x48\x2a\x2d\x29\xc9\xcf\x2b\x56\x54\x08\x18\x99\x1c\x00", 18)},
        {"assets/sound/dialog/ARS/arabic.name", 0, "arabic names", "arabic names"},
        {"assets/characters/customhero/behavior.unrecognized", 0, "custom behavior", "custom behavior"},
        {"assets/custom/new.zip", 0, "opaque game archive", "opaque game archive"}}));
    // CRC failures are reported per file and never expose a partially extracted
    // redirect/member. An independent valid sidecar/bank remains usable.
    write(mods / "40_damaged/audio.zip", normalZip({
        {"assets/sound/dialog/RUR/bad.fsb", 0, "good", "evil"},
        {"assets/sound/dialog/RUR/bad.name", 0, "valid sidecar", "valid sidecar"},
        {"assets/sound/dialog/RUR/good.fsb", 0, "valid bank", "valid bank"},
        {"assets/sound/dialog/RUR/good.name", 0, "good", "evil"},
        {"assets/custom/broken/file.new", 12, "bad compression", "bad compression"}}));
    for (const auto* language : {"PLP", "RUR", "ARS"}) CHECK(!fs::exists(game / "sound/dialog" / language));
    std::vector<std::string> messages;
    const auto enabled = Index::load(mods.wstring(), messages);
    CHECK(enabled.mods.size() == 4 && logged(messages, "Mod 00_disabled: disabled."));
    auto index = AssetIndex::build(game.wstring(), cache.wstring(), enabled.mods, messages);
    CHECK(logged(messages, "Mod 20_later: assets/sound/dialog/plp/shared.fsb ignored; 10_early already replaces it."));
    CHECK(logged(messages, "Mod 20_later: assets/sound/dialog/plp/shared.name ignored; 10_early already replaces it."));
    CHECK(logged(messages, "assets/sound/dialog/rur/bad.fsb not applied:"));
    CHECK(logged(messages, "assets/sound/dialog/rur/good.name not applied:"));
    CHECK(logged(messages, "assets/custom/broken/file.new not applied:"));
    const auto disk = [&](std::string_view path) { const auto* file = index.find(path); return file ? slurp(*file) : "<none>"; };
    for (const auto& [name, data] : std::vector<std::pair<std::string, std::string>>{
        {"plp/shared.fsb", "early bank"}, {"plp/shared.name", "early names"},
        {"plp/unique.fsb", "later unique bank"}, {"plp/unique.name", "later unique names"},
        {"rur/russian.fsb", "russian bank"}, {"rur/russian.name", "russian names"},
        {"ars/arabic.name", "arabic names"}, {"rur/good.fsb", "valid bank"}, {"rur/bad.name", "valid sidecar"}}) {
        const auto path = "sound/dialog/" + name;
        CHECK(disk(path) == data && served(index, path) == data);
    }
    const auto* packed = index.find("sound/dialog/ars/arabic.fsb");
    CHECK(packed && packed->find(L".cache") != std::wstring::npos && slurp(*packed).size() == 260);
    CHECK(served(index, "sound/dialog/ars/arabic.fsb").size() == 260);
    CHECK(!index.find("sound/dialog/rur/bad.fsb") && !index.member("sound/dialog/rur/bad.fsb"));
    CHECK(!index.find("sound/dialog/rur/good.name") && !index.member("sound/dialog/rur/good.name"));
    CHECK(!index.find("sound/dialog/jpn/disabled.fsb") && !index.member("sound/dialog/jpn/disabled.fsb"));
    CHECK(!index.find("custom/disabled/file.new") && !index.member("custom/disabled/file.new"));
    CHECK(!index.find("custom/broken/file.new") && !index.member("custom/broken/file.new"));
    CHECK(disk("sound/dialog/rur/readme.txt") == "not a native bank" && served(index, "sound/dialog/rur/readme.txt") == "not a native bank");
    CHECK(disk("other/table.name") == "not a sound sidecar" && served(index, "other/table.name") == "not a sound sidecar");
    CHECK(disk("characters/customhero/nested/rig.newformat") == "custom rig");
    CHECK(served(index, "characters/customhero/nested/rig.newformat") == "custom rig");
    CHECK(disk("characters/customhero/nested/unique.custom") == "custom component");
    CHECK(served(index, "characters/customhero/nested/unique.custom") == "custom component");
    CHECK(disk("characters/customhero/behavior.unrecognized") == "custom behavior");
    CHECK(served(index, "characters/customhero/behavior.unrecognized") == "custom behavior");
    CHECK(disk("custom/new.zip") == "opaque game archive" && served(index, "custom/new.zip") == "opaque game archive");
    CHECK(!index.find("sound/dialog/enu/native.fsb") && !index.member("sound/dialog/enu/native.fsb"));
    CHECK(slurp(game / "sound/dialog/ENU/native.fsb") == "factory bank");
    CHECK(slurp(game / "sound/dialog/ENU/native.name") == "factory names");
    for (const auto* language : {"PLP", "RUR", "ARS"}) CHECK(!fs::exists(game / "sound/dialog" / language));
    CHECK(index.hasDirectory("sound") && index.hasDirectory("sound/dialog") && index.hasDirectory("sound/dialog/rur"));
    CHECK(index.hasDirectory("sound/dialog/plp") && index.hasDirectory("sound/dialog/ars"));
    CHECK(index.hasDirectory("characters") && index.hasDirectory("characters/customhero/nested"));
    CHECK(index.hasDirectory("custom") && index.hasDirectory("other"));
    CHECK(!index.hasDirectory("custom/disabled") && !index.hasDirectory("custom/broken"));
    CHECK(!index.hasDirectory("characters/customhero/nest") && !index.hasDirectory("custom/new.zip"));
    CHECK(!index.hasDirectory("sound/dialog/ru") && !index.hasDirectory("sound/dialog/rur2"));
    CHECK(!index.hasDirectory("sound/dialog/jpn") && !index.hasDirectory("sound/dialog/enu"));
    CHECK(!index.hasDirectory("sound/dialog/plp/shared.fsb") && !index.hasDirectory("") && !index.hasDirectory("sound/dialog/rur/"));
    // A same-prefix file sorted before '/' must not hide the virtual directory.
    AssetIndex prefixes;
    prefixes.redirects = {{"sound/dialog/rur.name", L"names"}, {"sound/dialog/rur/a.fsb", L"bank"}, {"sound/dialog/rur2/b.fsb", L"other"}};
    CHECK(prefixes.hasDirectory("sound/dialog/rur") && !prefixes.hasDirectory("sound/dialog/ru"));
    if (packed) {
        const auto stamp = fs::last_write_time(*packed);
        const auto sidecar = fs::last_write_time(*index.find("sound/dialog/ars/arabic.name"));
        Sleep(20);
        messages.clear();
        const auto again = AssetIndex::build(game.wstring(), cache.wstring(), enabled.mods, messages);
        CHECK(again.find("sound/dialog/ars/arabic.fsb") && fs::last_write_time(*again.find("sound/dialog/ars/arabic.fsb")) == stamp);
        CHECK(again.find("sound/dialog/ars/arabic.name") && fs::last_write_time(*again.find("sound/dialog/ars/arabic.name")) == sidecar);
    }
}

int main() {
    std::string text;
    for (int index = 0; index < 20; ++index) text += "PS3 buttons! ";
    const unsigned char deflated[] = {0x0b,0x08,0x36,0x56,0x48,0x2a,0x2d,0x29,0xc9,0xcf,0x2b,0x56,0x54,0x08,0x18,0x99,0x1c,0x00};
    const std::string compressed(reinterpret_cast<const char*>(deflated), sizeof(deflated));
    CHECK(zip::crc32(text.data(), text.size()) == 0x10532e65);
    CHECK(zip::inflate(compressed, 260) == text);
    bool threw = false;
    try { zip::inflate("\xff\xff\xff", 10); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    wchar_t temp[MAX_PATH]{};
    GetTempPathW(MAX_PATH, temp);
    const fs::path root = fs::path(temp) / (L"literalcrab-assets-" + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(root);

    // Ordinary deflated and stored members read back; damaged data is refused.
    write(root / "normal.zip", normalZip({{"Dir/", 0, "", ""}, {"Dir/Deflated.txt", 8, text, compressed}, {"stored.bin", 0, "xyz", "xyz"}}));
    {
        zip::Archive normal((root / "normal.zip").wstring());
        CHECK(normal.entries().size() == 3 && normal.entries()[0].directory());
        CHECK(member(normal, "dir\\deflated.TXT") == text && member(normal, "stored.bin") == "xyz");
        CHECK(!normal.find("Deflated.txt"));
    }
    auto damaged = normalZip({{"a.bin", 0, "abc", "abd"}});
    write(root / "damaged.zip", damaged);
    threw = false;
    try { zip::Archive archive((root / "damaged.zip").wstring()); member(archive, "a.bin"); } catch (const std::exception&) { threw = true; }
    CHECK(threw);

    // Names.
    char path[maxAssetPath];
    auto length = assetPath(std::string_view("S:\\Games\\DI\\assets\\UI\\ui.zip"), path);
    CHECK(std::string_view(path, length) == "ui/ui.zip");
    length = assetPath(std::wstring_view(L".\\ASSETS\\fmv\\Intro.bik"), path);
    CHECK(std::string_view(path, length) == "fmv/intro.bik");
    CHECK(!assetPath(std::string_view("C:\\Game\\mods\\m\\assets\\ui\\ui.zip"), path));
    CHECK(!assetPath(std::string_view("C:\\Game\\LiteralCrab.ini"), path));
    CHECK(!assetPath(std::wstring_view(L"C:\\Gam\u00e9\\assets\\x.zip"), path));
    length = requestName("Textures\\MSSansSerif_12.MTB", path);
    CHECK(std::string_view(path, length) == "textures/mssansserif_12.mtb");
    CHECK(memberName("ui/textures/0389b6b276ccc3af.tbody") == "textures/0389b6b276ccc3af.tbody");
    CHECK(memberName("ui/controllerbuttons.mtb") == "ui/controllerbuttons.mtb");

    // A synthetic game and mods.
    const auto game = root / "game" / "assets";
    write(game / "ui" / "ui.zip", normalZip({{"controllerbuttons.mtb", 0, "xbox layout", "xbox layout"}}));
    write(game / "ui" / "icons" / "icons.zip", normalZip({{"icon.mtb", 0, "old", "old"}}));
    write(game / "fmv" / "intro.bik", "original video");
    write(game / "fmv" / "outro.bik", "original outro");
    const auto mods = root / "mods";
    // The PS3 button layout: a package with an assets tree, plus loose files.
    write(mods / "ps3" / "ui.zip", normalZip({{"assets/", 0, "", ""}, {"assets/UI/", 0, "", ""},
        {"assets/UI/ControllerButtons.mtb", 8, text, compressed}, {"assets/ui/textures/AAA.tbody", 0, "ps3 A", "ps3 A"},
        {"assets/fmv/outro.bik", 0, "packed outro", "packed outro"}, {"readme.txt", 0, "hi", "hi"}}));
    write(mods / "ps3" / "assets" / "fmv" / "intro.bik", "modded video");
    // A whole replacement archive, ordinary ZIP: served member by member.
    write(mods / "ps3" / "assets" / "ui" / "icons" / "icons.zip",
        normalZip({{"icon.mtb", 8, text, compressed}, {"textures/bbb.tbody", 0, "icon B", "icon B"}}));
    write(mods / "second" / "assets" / "ui" / "controllerbuttons.mtb", "second layout");
    write(mods / "second" / "assets" / "other" / "textures" / "aaa.tbody", "second A");
    write(mods / "second" / "assets" / "brand" / "new.lua", "return 1");
    write(mods / "third" / "notes.txt", "nothing usable");
    const std::vector<Mod> list = {
        {"ps3", "PS3 Buttons", (mods / "ps3").wstring(), 0},
        {"second", "Second", (mods / "second").wstring(), 0},
        {"third", "Third", (mods / "third").wstring(), 0},
    };
    std::vector<std::string> messages;
    auto index = AssetIndex::build(game.wstring(), (mods / ".cache").wstring(), list, messages);
    for (const auto& message : messages) std::printf("  %s\n", message.c_str());
    CHECK(logged(messages, "Mod ps3: ui.zip: 1 file(s) outside an assets folder ignored."));
    CHECK(logged(messages, "Mod second: assets/ui/controllerbuttons.mtb ignored; ps3 already replaces it."));
    CHECK(logged(messages, "Mod second: assets/other/textures/aaa.tbody ignored; ps3 already replaces it."));
    CHECK(logged(messages, "Mod ps3: 5 asset file(s)."));
    CHECK(logged(messages, "Mod second: 1 asset file(s)."));
    CHECK(logged(messages, "Mod third: no scripts or usable asset files found."));

    // Requests by name, never touching the game's archives.
    CHECK(index.members.size() == 7);
    CHECK(served(index, "ui/controllerbuttons.mtb") == text);
    CHECK(served(index, "textures/aaa.tbody") == "ps3 A");
    CHECK(served(index, "ui/icons/icon.mtb") == text);
    CHECK(served(index, "textures/bbb.tbody") == "icon B");
    CHECK(served(index, "brand/new.lua") == "return 1");
    CHECK(!index.member("ui/ui.zip") && !index.member("ui/icons/icons.zip"));
    // Files the game opens from disk are redirected; a packaged one is extracted once.
    CHECK(index.redirects.size() == 6);
    const auto* intro = index.find("fmv/intro.bik");
    CHECK(intro && slurp(*intro) == "modded video");
    const auto* outro = index.find("fmv/outro.bik");
    CHECK(outro && slurp(*outro) == "packed outro" && outro->find(L".cache") != std::wstring::npos);
    CHECK(served(index, "fmv/intro.bik") == "modded video" && served(index, "fmv/outro.bik") == "packed outro");
    CHECK(index.find("ui/controllerbuttons.mtb") && slurp(*index.find("ui/controllerbuttons.mtb")) == text);
    CHECK(index.find("brand/new.lua") && slurp(*index.find("brand/new.lua")) == "return 1");
    if (outro) {
        const auto extracted = fs::last_write_time(*outro);
        Sleep(20);
        messages.clear();
        index = AssetIndex::build(game.wstring(), (mods / ".cache").wstring(), list, messages);
        CHECK(index.find("fmv/outro.bik") && fs::last_write_time(*index.find("fmv/outro.bik")) == extracted);
    }
    // A loose file edited while the game runs is read fresh on the next request.
    write(mods / "second" / "assets" / "brand" / "new.lua", "return 2");
    CHECK(served(index, "brand/new.lua") == "return 2");

    // The runtime lookups the hooks use.
    const auto expectedIntro = *index.find("fmv/intro.bik");
    setAssetIndexForTest(std::move(index));
    const auto* found = redirectedAsset("C:\\Program Files\\Game\\assets\\FMV\\Intro.bik");
    CHECK(found && *found == expectedIntro);
    CHECK(redirectedAsset(L"assets\\fmv\\outro.bik") != nullptr);
    CHECK(!redirectedAsset("assets\\ui\\ui.zip") && !redirectedAsset(static_cast<const char*>(nullptr)));
    std::string data;
    CHECK(readAssetMember("UI\\ControllerButtons.mtb", data) && data == text);
    CHECK(readAssetMember("Textures/AAA.tbody", data) && data == "ps3 A");
    CHECK(!readAssetMember("ui/other.mtb", data) && !readAssetMember(nullptr, data));
    setAssetIndexForTest({});

    addedContentFiles(root);

    fs::remove_all(root);
    if (failures) return 1;
    std::puts("PASS: ZIP reading, request names, redirects and member reads");
    return 0;
}
