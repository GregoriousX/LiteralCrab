// SPDX-License-Identifier: GPL-3.0-only
// ZIP reading/writing, asset placement into the game's archives, the rebuild cache and the
// redirect lookup, against a synthetic game folder.
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
static std::string hex(const zip::Md5& digest) {
    std::string text;
    char part[3];
    for (const auto byte : digest) { std::snprintf(part, sizeof(part), "%02x", byte); text += part; }
    return text;
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

static void gameZip(const fs::path& file, const std::vector<std::pair<std::string, std::string>>& files) {
    fs::create_directories(file.parent_path());
    std::vector<zip::Member> members;
    for (const auto& [name, data] : files) {
        zip::Member member;
        member.name = name;
        member.crc = zip::crc32(data.data(), data.size());
        member.compressedSize = member.size = static_cast<std::uint32_t>(data.size());
        member.md5 = zip::md5(data.data(), data.size());
        member.write = [&data](const auto& sink) { sink(data.data(), data.size()); };
        members.push_back(std::move(member));
    }
    zip::writeGameArchive(file.wstring(), members, 0x742F, 0x99C3);
}

static std::string member(const zip::Archive& archive, const char* name) {
    const auto* entry = archive.find(name);
    return entry ? archive.read(*entry) : "<missing>";
}

int main() {
    // Primitives.
    CHECK(hex(zip::md5("", 0)) == "d41d8cd98f00b204e9800998ecf8427e");
    CHECK(hex(zip::md5("abc", 3)) == "900150983cd24fb0d6963f7d28e17f72");
    for (const auto& [size, expected] : {std::pair<std::size_t, const char*>{55, "ef1772b6dff9a122358552954ad0df65"},
             {56, "3b0c8ac703f828b04c6c197006d17218"}, {64, "014842d480b571495a4a0363793f7367"}, {1000, "cabe45dcc9ae5b66ba86600cca6b8ba8"}}) {
        const std::string text(size, 'a');
        CHECK(hex(zip::md5(text.data(), text.size())) == expected);
    }
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

    // An ordinary deflated/stored ZIP reads back; the game layout writes and reads back.
    write(root / "normal.zip", normalZip({{"Dir/", 0, "", ""}, {"Dir/Deflated.txt", 8, text, compressed}, {"stored.bin", 0, "xyz", "xyz"}}));
    {
        zip::Archive normal((root / "normal.zip").wstring());
        CHECK(normal.entries().size() == 3 && normal.entries()[0].directory());
        CHECK(member(normal, "dir\\deflated.TXT") == text && member(normal, "stored.bin") == "xyz");
        CHECK(!normal.find("Deflated.txt"));
    }
    gameZip(root / "game.zip", {{"a.mtb", "alpha"}, {"textures/b.tbody", text}});
    {
        const auto bytes = slurp(root / "game.zip");
        CHECK(bytes.compare(0, 4, "PK\x05\x06") == 0);
        // The front copy of the end record and directory matches the trailing one.
        const auto directorySize = static_cast<unsigned char>(bytes[12]) | static_cast<unsigned char>(bytes[13]) << 8;
        CHECK(bytes.compare(bytes.size() - 22, 22, bytes, 0, 22) == 0);
        CHECK(bytes.compare(bytes.size() - 22 - directorySize, directorySize, bytes, 22, directorySize) == 0);
        zip::Archive game((root / "game.zip").wstring());
        CHECK(game.entries().size() == 2 && game.entries()[1].hasMd5 && hex(game.entries()[1].md5) == "d0c37d0b3d0ffdfea986011b433f8311");
        CHECK(member(game, "textures/b.tbody") == text);
    }

    // File names reduce to their path below the last assets folder.
    char path[maxAssetPath];
    auto length = assetPath(std::string_view("S:\\Games\\DI\\assets\\UI\\ui.zip"), path);
    CHECK(std::string_view(path, length) == "ui/ui.zip");
    length = assetPath(std::string_view("assets/fmv/intro.bik"), path);
    CHECK(std::string_view(path, length) == "fmv/intro.bik");
    length = assetPath(std::wstring_view(L".\\ASSETS\\ui\\x.zip"), path);
    CHECK(std::string_view(path, length) == "ui/x.zip");
    CHECK(!assetPath(std::string_view("C:\\Game\\mods\\m\\assets\\ui\\ui.zip"), path));
    CHECK(!assetPath(std::string_view("C:\\Game\\LiteralCrab.ini"), path));
    CHECK(!assetPath(std::wstring_view(L"C:\\Gam\u00e9\\assets\\ui\\ui.zip"), path));
    CHECK(!assetPath(std::string_view("C:\\Game\\assets\\"), path));

    // A synthetic game and two mods.
    const auto game = root / "game" / "assets";
    gameZip(game / "ui" / "ui.zip", {{"controllerbuttons.mtb", "xbox layout"}, {"textures/aaa.tbody", "xbox A"}, {"textures/bbb.tbody", "xbox B"}});
    gameZip(game / "ui" / "icons" / "icons.zip", {{"icon.tbody", "old icon"}});
    gameZip(game / "avatars" / "a.zip", {{"y.mtb", "y"}});
    gameZip(game / "avatars" / "b.zip", {{"x.mtb", "x"}});
    write(game / "fmv" / "intro.bik", "original video");
    const auto mods = root / "mods";
    write(mods / "ps3" / "ui.zip", normalZip({{"assets/", 0, "", ""}, {"assets/ui/", 0, "", ""},
        {"assets/UI/Textures/AAA.tbody", 8, text, compressed}, {"assets/ui/new.tbody", 0, "brand new", "brand new"},
        {"readme.txt", 0, "hi", "hi"}}));
    write(mods / "ps3" / "assets" / "fmv" / "intro.bik", "modded video");
    write(mods / "ps3" / "assets" / "ui" / "icons" / "icons.zip", normalZip({{"icon.tbody", 0, "new icon", "new icon"}}));
    write(mods / "second" / "assets" / "ui" / "textures" / "aaa.tbody", "second A");
    write(mods / "second" / "assets" / "avatars" / "x.mtb", "modded x");
    write(mods / "second" / "assets" / "avatars" / "nothere.mtb", "?");
    write(mods / "second" / "assets" / "unknown" / "file.bin", "?");
    write(mods / "third" / "notes.txt", "nothing usable");
    const std::vector<Mod> list = {
        {"ps3", "PS3 Buttons", (mods / "ps3").wstring(), 0},
        {"second", "Second", (mods / "second").wstring(), 0},
        {"third", "Third", (mods / "third").wstring(), 0},
    };
    std::vector<std::string> messages;
    auto index = AssetIndex::build(game.wstring(), (mods / ".cache").wstring(), list, messages);
    for (const auto& message : messages) std::printf("  %s\n", message.c_str());
    CHECK(index.redirects.size() == 4);
    CHECK(logged(messages, "Mod ps3: ui.zip: 1 file(s) outside an assets folder ignored."));
    CHECK(logged(messages, "Mod second: assets/ui/textures/aaa.tbody ignored; ps3 already replaces it."));
    CHECK(logged(messages, "Mod second: assets/avatars/nothere.mtb ignored; assets/avatars/ holds several archives"));
    CHECK(logged(messages, "Mod second: assets/unknown/file.bin ignored; no game file or archive matches this path."));
    CHECK(logged(messages, "Mod ps3: 4 asset file(s)."));
    CHECK(logged(messages, "Mod second: 1 asset file(s)."));
    CHECK(logged(messages, "Mod third: no scripts or usable asset files found."));

    const auto* video = index.find("fmv/intro.bik");
    CHECK(video && slurp(*video) == "modded video");
    const auto* ui = index.find("ui/ui.zip");
    CHECK(ui && ui->find(L".cache") != std::wstring::npos);
    if (ui) {
        zip::Archive merged(*ui);
        CHECK(slurp(*ui).compare(0, 4, "PK\x05\x06") == 0);
        CHECK(merged.entries().size() == 4);
        CHECK(merged.entries()[0].name == "controllerbuttons.mtb" && member(merged, "controllerbuttons.mtb") == "xbox layout");
        // Replaced in place with the game's own member name; md5 describes the new data.
        CHECK(merged.entries()[1].name == "textures/aaa.tbody" && member(merged, "textures/aaa.tbody") == text);
        CHECK(merged.entries()[1].hasMd5 && hex(merged.entries()[1].md5) == "d0c37d0b3d0ffdfea986011b433f8311");
        CHECK(merged.entries()[1].method == 8);  // Compressed bytes copied, not recompressed.
        CHECK(member(merged, "textures/bbb.tbody") == "xbox B");
        CHECK(merged.entries()[3].name == "new.tbody" && member(merged, "new.tbody") == "brand new");
        const std::string newData = "brand new";
        CHECK(hex(merged.entries()[3].md5) == hex(zip::md5(newData.data(), newData.size())));
    }
    const auto* icons = index.find("ui/icons/icons.zip");
    CHECK(icons && slurp(*icons).compare(0, 4, "PK\x05\x06") == 0);
    if (icons) CHECK(member(zip::Archive(*icons), "icon.tbody") == "new icon");
    const auto* avatars = index.find("avatars/b.zip");
    if (avatars) CHECK(member(zip::Archive(*avatars), "x.mtb") == "modded x");
    CHECK(avatars && !index.find("avatars/a.zip"));

    // Unchanged inputs reuse the cache; a changed mod file rebuilds only its archive.
    const auto uiTime = fs::last_write_time(*ui), avatarsTime = fs::last_write_time(*avatars);
    Sleep(20);
    messages.clear();
    index = AssetIndex::build(game.wstring(), (mods / ".cache").wstring(), list, messages);
    CHECK(fs::last_write_time(*index.find("ui/ui.zip")) == uiTime && fs::last_write_time(*index.find("avatars/b.zip")) == avatarsTime);
    write(mods / "second" / "assets" / "avatars" / "x.mtb", "modded x, version 2");
    index = AssetIndex::build(game.wstring(), (mods / ".cache").wstring(), list, messages);
    CHECK(fs::last_write_time(*index.find("ui/ui.zip")) == uiTime);
    CHECK(member(zip::Archive(*index.find("avatars/b.zip")), "x.mtb") == "modded x, version 2");

    // The hooks' lookup, without installing hooks.
    const auto expectedUi = *index.find("ui/ui.zip");
    setAssetIndexForTest(std::move(index));
    const auto* found = redirectedAsset("C:\\Program Files\\Game\\assets\\UI\\UI.zip");
    CHECK(found && *found == expectedUi);
    CHECK(redirectedAsset(L"assets\\fmv\\intro.bik") != nullptr);
    CHECK(!redirectedAsset("assets\\ui\\other.zip") && !redirectedAsset(static_cast<const char*>(nullptr)));
    setAssetIndexForTest({});

    fs::remove_all(root);
    if (failures) return 1;
    std::puts("PASS: ZIP formats, asset placement, cache reuse and redirect lookup");
    return 0;
}
