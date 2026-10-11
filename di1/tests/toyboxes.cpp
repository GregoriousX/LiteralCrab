// SPDX-License-Identifier: GPL-3.0-only
// Save file formats (checksum, console cipher, CMP1 container), Toy Box conversions and the
// package library against synthetic packages, including damaged ones.
#include "mods/zip.hpp"
#include "toyboxes/library.hpp"
#include "toyboxes/save_file.hpp"
#include "toyboxes/server.hpp"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace crabe;
using namespace crabe::toyboxes;
namespace fs = std::filesystem;

static int failures = 0;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

static std::vector<std::string> messages;
static void record(const char* format, ...) {
    char buffer[2048];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    messages.emplace_back(buffer);
}
static bool logged(std::string_view text) {
    for (const auto& message : messages) if (message.find(text) != std::string::npos) return true;
    std::printf("  missing message: %.*s\n", static_cast<int>(text.size()), text.data());
    return false;
}
static std::string unhex(std::string_view text) {
    std::string out;
    for (std::size_t at = 0; at + 1 < text.size(); at += 2) out.push_back(static_cast<char>(std::stoi(std::string(text.substr(at, 2)), nullptr, 16)));
    return out;
}
static void write(const fs::path& file, std::string_view data) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary).write(data.data(), static_cast<std::streamsize>(data.size()));
}
static void put16(std::string& out, unsigned value) { out += static_cast<char>(value & 0xFF); out += static_cast<char>((value >> 8) & 0xFF); }
static void put32(std::string& out, std::uint32_t value) { put16(out, value & 0xFFFF); put16(out, value >> 16); }
// A stored ZIP as common tools write it.
static std::string zipOf(const std::vector<std::pair<std::string, std::string>>& items) {
    std::string body, directory;
    for (const auto& [name, data] : items) {
        const auto crc = mods::zip::crc32(data.data(), data.size());
        const auto offset = static_cast<std::uint32_t>(body.size());
        put32(body, 0x04034B50); put16(body, 20); put16(body, 0); put16(body, 0); put16(body, 0); put16(body, 0x5A21);
        put32(body, crc); put32(body, static_cast<std::uint32_t>(data.size())); put32(body, static_cast<std::uint32_t>(data.size()));
        put16(body, static_cast<unsigned>(name.size())); put16(body, 0); body += name; body += data;
        put32(directory, 0x02014B50); put16(directory, 63); put16(directory, 20); put16(directory, 0); put16(directory, 0);
        put16(directory, 0); put16(directory, 0x5A21); put32(directory, crc); put32(directory, static_cast<std::uint32_t>(data.size()));
        put32(directory, static_cast<std::uint32_t>(data.size())); put16(directory, static_cast<unsigned>(name.size()));
        put16(directory, 0); put16(directory, 0); put16(directory, 0); put16(directory, 0); put32(directory, 32); put32(directory, offset);
        directory += name;
    }
    std::string end;
    put32(end, 0x06054B50); put16(end, 0); put16(end, 0); put16(end, static_cast<unsigned>(items.size())); put16(end, static_cast<unsigned>(items.size()));
    put32(end, static_cast<std::uint32_t>(directory.size())); put32(end, static_cast<std::uint32_t>(body.size())); put16(end, 0);
    return body + directory + end;
}
static std::string saveFile(std::string_view text) { return pcFile(encode(text)); }
static std::string nul(std::string text) { text.push_back('\0'); return text; }

// A play set save of the Toy Box as the game writes it (trimmed to the parts that matter).
static const std::string slotData = nul(
    "zoneglobal =\r\n{\r\n\tmeridian =\r\n\t{\r\n\t\tMeridianGraphs =\r\n\t\t{\r\n\t\t}\r\n\t}\r\n}\r\n"
    "zone =\r\n{\r\n\t_rumpusroom_blank_ =\r\n\t{\r\n"
    "\t\trumpus =\r\n\t\t{\r\n\t\t\tRumpusSaveGame =\r\n\t\t\t{\r\n"
    "\t\t\t\tCurrentEnv = \"rumpusroom_blank\"\r\n\t\t\t\tNUM_CHILDREN = 3\r\n\t\t\t\tRR_SAVE_VERSION = 31\r\n"
    "\t\t\t\tCreateId =\r\n\t\t\t\t[\r\n\t\t\t\t\t1537 583 1537\r\n\t\t\t\t]\r\n\t\t\t}\r\n\t\t}\r\n"
    "\t\tmeridian =\r\n\t\t{\r\n\t\t\tMeridianGraphs =\r\n\t\t\t{\r\n\t\t\t\tRumpusRoom_Blank =\r\n\t\t\t\t{\r\n\t\t\t\t}\r\n\t\t\t}\r\n\t\t}\r\n"
    "\t\tCActorSaveData =\r\n\t\t{\r\n\t\t\tVERSION = 1\r\n\t\t}\r\n\t}\r\n}\r\nHannahStats =\r\n{\r\n\tPLAYSETDATACOUNT = 1\r\n}\r\n");
static const std::string ugcData = nul(
    "zone =\r\n{\r\n\t_rumpusroom_blank_ =\r\n\t{\r\n"
    "\t\tRumpusSaveGame =\r\n\t\t{\r\n"
    "\t\t\tCurrentEnv = \"rumpusroom_blank\"\r\n\t\t\tNUM_CHILDREN = 3\r\n\t\t\tRR_SAVE_VERSION = 31\r\n"
    "\t\t\tCreateId =\r\n\t\t\t[\r\n\t\t\t\t1537 583 1537\r\n\t\t\t]\r\n\t\t}\r\n"
    "\t\tMeridianGraphs =\r\n\t\t{\r\n\t\t\tRumpusRoom_Blank =\r\n\t\t\t{\r\n\t\t\t}\r\n\t\t}\r\n\t}\r\n}\r\n");
static const std::string slotHeader = nul(
    "CSaveGameSlot =\r\n{\r\n\tISVALID = 1\r\n\tTIMESTAMP = \"05:32:43 PM\"\r\n\tDATESTAMP = \"09-08-2013\"\r\n"
    "\tTIME = 1378661563\r\n\tLASTLEVEL = \"RumpusRoom_Blank\"\r\n\tRAWLEVELNAME = \"RumpusRoom_Blank\"\r\n"
    "\tUSERNAME = \"Driving Test\"\r\n\tDESCRIPTION = \"Laps\"\r\n\tCLOUDID = \"\"\r\n\tTIMEPLAYED = 115\r\n}\r\n");
static const std::string slotScreen = nul(
    "CSaveGameSlot =\r\n{\r\n\tSCREENSHOT = \r\n\t$\r\n\t\t00ff00ff00ff00ff\r\n\t$\r\n\tSCREENSHOT_SIZE = 8\r\n"
    "\tSCREENSHOT_WIDTH = 4\r\n\tSCREENSHOT_HEIGHT = 4\r\n\tSCREENSHOT_FORMAT = 1\r\n\tSCREENSHOT_PLATFORM = 3\r\n}\r\n");
static const std::string ugcHeaderText = nul(
    "CSaveGameUGC =\r\n{\r\n\tCLOUDID = \"3fd5\"\r\n\tAUTHOREDNAME = \"Bonnie's Room\"\r\n\tAUTHOREDDESC = \"Find the key\"\r\n"
    "\tCREATOR = \"unknown\"\r\n\tTIMESTRING = \"08:52:28 PM\"\r\n\tDATESTRING = \"19-09-2014\"\r\n\tTIMESTAMP = 1411152748\r\n"
    "\tLASTLEVEL = \"RumpusRoom_Blank\"\r\n\tOBJECTCOUNTS = \"{\"\"1537\"\": 2}\"\r\n\tTARGET = \"private\"\r\n}\r\n");

static std::string otherData() {
    auto data = ugcData;
    data.replace(data.find("1537 583 1537"), 13, "1537 583 1538");
    return data;
}
static std::string onlineData() {
    auto data = ugcData;
    data.replace(data.find("1537 583 1537"), 13, "1537 583 1539");
    return data;
}
static std::string looseSlot() {
    auto data = slotData;
    data.replace(data.find("1537 583 1537"), 13, "1537 583 1540");
    return data;
}
static bool knownWorld(const std::string& world) { return world == "rumpusroom_blank"; }
static std::string cached(const Toybox& toybox, int file) {
    std::ifstream input(toybox.files[file], std::ios::binary);
    return inflate(decodeFile(std::string(std::istreambuf_iterator<char>(input), {})));
}

static int run() {
    // The game's CMP1 checksum (Bob Jenkins' lookup2).
    CHECK(lookup2("", 0) == 0xbd49d10d);
    CHECK(lookup2("a", 1) == 0x29eec818);
    CHECK(lookup2("Disney Infinity", 15) == 0xfe3e2d2c);
    CHECK(lookup2("CMP1 toy box hash test 0123456789", 33) == 0x2fef568c);

    // Phelix: a PC (little-endian) vector produced by the executable's own routine, and the
    // first words of a real PS3 Toy Box Share file.
    auto text = unhex("ea69bf8d3e1f92fe1e627908f014fe32");
    phelixDecrypt(reinterpret_cast<unsigned char*>(text.data()), text.size(), false);
    CHECK(text == unhex("434d5031000074ef0000aaaa00000000"));
    text = unhex("4f5dd23eb79fb40fcb8fbd1116c2a4603d03b54292975124462c2f6392dd80a8");
    phelixDecrypt(reinterpret_cast<unsigned char*>(text.data()), text.size(), true);
    CHECK(text == unhex("434d5031000074ef000020a8a0ca936b5fa5d7957801ed9d5baf1cc771c7f37a"));

    // Container round trip, including a text longer than one stored deflate block.
    std::string large(200000, 'x');
    for (std::size_t at = 0; at < large.size(); at += 7) large[at] = static_cast<char>('a' + at % 26);
    for (const std::string* sample : {&ugcData, static_cast<const std::string*>(&large)}) {
        const auto file = saveFile(*sample);
        CHECK(file.size() % 0x40 == 0);
        CHECK(inflate(decodeFile(file)) == *sample);
    }
    auto damaged = saveFile(ugcData);
    damaged[0x60] ^= 1;
    bool rejected = false;
    try { decodeFile(damaged); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { decodeFile("not a save file at all, but long enough to be checked.........................."); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);

    // Recognition and conversions.
    CHECK(classify(slotData) == Kind::SlotData);
    CHECK(classify(ugcData) == Kind::UgcData);
    CHECK(classify(slotHeader) == Kind::SlotHeader);
    CHECK(classify(slotScreen) == Kind::SlotScreen);
    CHECK(classify(ugcHeaderText) == Kind::UgcHeader);
    CHECK(classify("HelpManager =\r\n{\r\n}\r\n") == Kind::Unknown);
    CHECK(ugcDataFromSlot(slotData) == ugcData);
    CHECK(world(ugcData) == "rumpusroom_blank");
    CHECK(toyBoxWorld("rumpusroom_blank") && toyBoxWorld("RumpusRoom_Maze") && toyBoxWorld("rumpusroom_cars"));
    CHECK(!toyBoxWorld("rumpusroom_flow01") && !toyBoxWorld("rumpusroom_hh") && !toyBoxWorld("rumpusroom_menu") && !toyBoxWorld(""));
    CHECK(field(slotHeader, "USERNAME") == "Driving Test");
    CHECK(field(slotHeader, "TIME") == "1378661563");
    const auto header = ugcHeader(ugcData, "Driving \"Test\"", "Laps", "05:32:43 PM", "09-08-2013", "1378661563", "RumpusRoom_Blank",
        publicTarget);
    CHECK(classify(header) == Kind::UgcHeader);
    CHECK(field(header, "AUTHOREDNAME") == "Driving Test");
    CHECK(field(header, "TARGET") == "public");
    CHECK(header.find("\tOBJECTCOUNTS = \"{\"\"583\"\": 1,\"\"1537\"\": 2}\"\r\n") != std::string::npos);
    CHECK(field(withTarget(ugcHeaderText, publicTarget), "TARGET") == "public");
    CHECK(field(withTarget(ugcHeaderText, publicTarget), "CLOUDID") == "3fd5");
    CHECK(field(withTarget(header, privateTarget), "TARGET") == "private");
    CHECK(ugcScreen(slotScreen).rfind("CSaveGameUGC =\r\n{\r\n\tSCREENSHOT = ", 0) == 0);

    // Untrusted content: what the game would parse is checked first.
    validateData(ugcData);  // Throws on failure; reaching the next line is the check.
    auto lying = ugcData;
    lying.replace(lying.find("NUM_CHILDREN = 3"), 16, "NUM_CHILDREN = 9");
    rejected = false;
    try { validateData(lying); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    auto huge = ugcData;
    huge.replace(huge.find("NUM_CHILDREN = 3"), 16, "NUM_CHILDREN = 99999999");
    rejected = false;
    try { validateData(huge); } catch (const std::exception&) { rejected = true; }
    CHECK(rejected);
    CHECK(validScreen(slotScreen));
    auto stretched = slotScreen;
    stretched.replace(stretched.find("WIDTH = 4"), 9, "WIDTH = 4096");
    CHECK(!validScreen(stretched));
    auto shortPixels = slotScreen;
    shortPixels.replace(shortPixels.find("00ff00ff00ff00ff"), 16, "00ff00ff");
    CHECK(!validScreen(shortPixels));
    // A package's own header text never reaches the game: names are cleaned and limited.
    const auto rebuilt = ugcHeader(ugcData, std::string(500, 'A') + "\r\n\tTARGET = \"x\"", std::string(5000, 'd'), "", "", "1", "Other_Level",
        privateTarget);
    CHECK(field(rebuilt, "AUTHOREDNAME").size() == 63);
    CHECK(field(rebuilt, "AUTHOREDDESC").size() == 255);
    CHECK(field(rebuilt, "TARGET") == "private");

    // The library: any package and member names, both layouts, damaged members and packages.
    const auto root = fs::temp_directory_path() / ("literalcrab_toyboxes_" + std::to_string(GetCurrentProcessId()));
    fs::remove_all(root);
    const auto cache = root / "toyboxes" / ".cache";
    write(root / "toyboxes" / "My saves.zip", zipOf({
        {"saves/SRR4", saveFile(slotData)}, {"saves/SHRR4", saveFile(slotHeader)}, {"saves/SCRR4", saveFile(slotScreen)},
        {"saves/SRR5", damaged}, {"saves/SHRR5", saveFile(slotHeader)},
        {"readme.txt", "Toy Boxes from my console"}}));
    write(root / "toyboxes" / "shared" / "deep" / "Bonnie.zip", zipOf({
        {"U1", saveFile(otherData())}, {"UH1", saveFile(ugcHeaderText)},
        {"other world/U2", saveFile(nul("zone =\r\n{\r\n\t_rumpusroom_moon_ =\r\n\t{\r\n\t\tRumpusSaveGame =\r\n\t\t{\r\n"
            "\t\t\tCurrentEnv = \"rumpusroom_moon\"\r\n\t\t\tNUM_CHILDREN = 0\r\n\t\t\tRR_SAVE_VERSION = 31\r\n\t\t\tCreateId =\r\n\t\t\t[\r\n\t\t\t]\r\n\t\t}\r\n\t}\r\n}\r\n"))}}));
    write(root / "toyboxes" / "broken.zip", "PK\x03\x04 this is not a complete archive");
    write(root / "toyboxes" / "duplicate.zip", zipOf({{"a/U7", saveFile(otherData())}, {"a/UH7", saveFile(ugcHeaderText)}}));
    // Loose save files under any names, beside other files: their folder is one package.
    write(root / "toyboxes" / "loose" / "SRR9", saveFile(looseSlot()));
    write(root / "toyboxes" / "loose" / "My header", saveFile(nul(std::string(slotHeader).replace(slotHeader.find("Driving Test"), 12, "Loose Box"))));
    write(root / "toyboxes" / "loose" / "notes.txt", "not a save file, and long enough to be looked at by the scanner......................");
    write(cache / "servers" / "example.org" / "toyboxes" / "online.zip", zipOf({{"U3", saveFile(onlineData())}, {"UH3", saveFile(ugcHeaderText)}}));
    const std::vector<Source> roots = {{(root / "toyboxes").wstring(), false}, {(cache / "servers").wstring(), true}};
    auto library = buildLibrary(roots, cache.wstring(), &knownWorld, &record);
    CHECK(library.size() == 4);
    if (library.size() == 4) {
        // Packages in path order, a folder's files before its subfolders: duplicate.zip holds
        // Bonnie's Room before shared\deep\Bonnie.zip does, and comes before "My saves.zip".
        const auto& bonnie = library[0];
        const auto& driving = library[1];
        CHECK(bonnie.name == "Bonnie's Room");
        CHECK(bonnie.files[2].empty());
        CHECK(!bonnie.online && field(cached(bonnie, 1), "TARGET") == "private");
        CHECK(driving.name == "Driving Test");
        CHECK(!driving.files[2].empty());
        CHECK(cached(driving, 0) == ugcData);
        CHECK(field(cached(driving, 1), "AUTHOREDNAME") == "Driving Test");
        if (!driving.files[2].empty()) CHECK(cached(driving, 2).rfind("CSaveGameUGC", 0) == 0);
        CHECK(!driving.online && field(cached(driving, 1), "TARGET") == "private");
        // Server mirrors come after the local folder and are listed among Disney's Toy Boxes.
        CHECK(library[2].name == "Loose Box" && !library[2].online);
        CHECK(library[3].online && field(cached(library[3], 1), "TARGET") == "public");
    }
    CHECK(logged("broken.zip skipped"));
    CHECK(logged("saves/SRR5 skipped"));
    CHECK(logged("not a Toy Box (rumpusroom_moon)"));
    CHECK(logged("1 duplicate(s) skipped"));
    CHECK(logged("5 package(s) converted"));
    // Unchanged packages come from the cache; a removed package leaves no cache files.
    messages.clear();
    fs::remove(root / "toyboxes" / "duplicate.zip");
    library = buildLibrary(roots, cache.wstring(), &knownWorld, &record);
    CHECK(library.size() == 4);
    CHECK(logged("0 package(s) converted"));
    std::size_t lists = 0;
    for (const auto& item : fs::directory_iterator(cache)) lists += item.path().extension() == ".lst";
    CHECK(lists == 4);  // broken.zip has no list; duplicate.zip's was removed.
    // Changing a loose file converts its folder again.
    messages.clear();
    write(root / "toyboxes" / "loose" / "SRR9", saveFile(looseSlot() + " "));
    library = buildLibrary(roots, cache.wstring(), &knownWorld, &record);
    CHECK(logged("1 package(s) converted"));
    fs::remove_all(root);

    // Server mirroring needs a web server; LITERALCRAB_TOYBOX_SERVERS names test URLs and
    // LITERALCRAB_TOYBOX_PACKAGES how many packages they hold in total.
    wchar_t urls[2048]{}, expected[32]{};
    if (GetEnvironmentVariableW(L"LITERALCRAB_TOYBOX_SERVERS", urls, 2048) &&
        GetEnvironmentVariableW(L"LITERALCRAB_TOYBOX_PACKAGES", expected, 32)) {
        std::vector<std::wstring> list;
        for (wchar_t* context{}, *token = wcstok_s(urls, L" ", &context); token; token = wcstok_s(nullptr, L" ", &context))
            list.emplace_back(token);
        const auto mirror = fs::temp_directory_path() / ("literalcrab_servers_" + std::to_string(GetCurrentProcessId()));
        messages.clear();
        CHECK(syncServers(list, mirror.wstring(), &record));
        std::size_t zips = 0;
        for (const auto& item : fs::recursive_directory_iterator(mirror)) zips += item.path().extension() == ".zip";
        CHECK(zips == std::wcstoul(expected, nullptr, 10));
        messages.clear();
        CHECK(!syncServers(list, mirror.wstring(), &record));  // Unchanged: nothing downloaded again.
        for (const auto& message : messages) std::printf("  %s\n", message.c_str());
        fs::remove_all(mirror);
    }

    if (failures) std::printf("%d check(s) failed.\n", failures);
    else std::printf("Toy Box tests passed.\n");
    return failures ? 1 : 0;
}

int main() {
    try {
        return run();
    } catch (const std::exception& failure) {
        std::printf("FAIL: unexpected exception: %s\n", failure.what());
        return 1;
    }
}
