// SPDX-License-Identifier: GPL-3.0-only
// Exercise the real hooked Win32 boundary in isolated processes, without a game
// installation, junctions, or a language-specific extension allowlist.
#include "mods/file_redirect.hpp"
#include "minhook/MinHook.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace crabe::mods;
namespace fs = std::filesystem;
static int failures{};
static std::vector<std::string> logs;
using OpenW = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static OpenW otherOpen{};
static HANDLE WINAPI otherHook(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
    DWORD disposition, DWORD flags, HANDLE file) { return otherOpen(name, access, share, security, disposition, flags, file); }
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s (error %lu)\n", __LINE__, #c, GetLastError()); ++failures; } } while (0)
static void logger(const char* format, ...) {
    char line[4096];
    va_list args; va_start(args, format); vsnprintf(line, sizeof(line), format, args); va_end(args);
    logs.emplace_back(line);
    SetLastError(ERROR_ACCESS_DENIED); // The hook must preserve the API's error.
}
static void write(const fs::path& file, const std::string& bytes) {
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << bytes;
}
static std::string read(const fs::path& path, bool wide) {
    const auto file = wide ? CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr)
        : CreateFileA(path.string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return {};
    char bytes[128]; DWORD done{};
    CHECK(ReadFile(file, bytes, sizeof(bytes), &done, nullptr));
    CloseHandle(file);
    return {bytes, done};
}
static bool contains(const std::string& token) {
    for (const auto& line : logs) if (line.find(token) != std::string::npos) return true;
    return false;
}
// Optional local-pack probe: --pack <mod-folder> <locale>. Reads every real
// bank/sidecar through a synthetic native assets path that has never existed.
// It checks file delivery, not FMOD decoding or audible playback.
static std::string wholeFile(const fs::path& path) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(file != INVALID_HANDLE_VALUE);
    if (file == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    const bool valid = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart <= (512ll << 20);
    CHECK(valid);
    std::string bytes;
    if (valid) {
        bytes.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD done{};
        CHECK(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &done, nullptr) && done == bytes.size());
    }
    CloseHandle(file);
    return bytes;
}
static int probePack(const fs::path& mod, const std::string& locale, const fs::path& root) {
    const auto assets = root / "game/assets";
    fs::create_directories(assets);
    std::vector<std::string> messages;
    auto index = AssetIndex::build(assets.wstring(), (root / "cache").wstring(),
        {{mod.filename().string(), "Local pack probe", mod.wstring(), 0, {}}}, messages);
    std::string prefix = "sound/dialog/" + locale + '/';
    for (auto& ch : prefix) if (ch >= 'A' && ch <= 'Z') ch += 32;
    std::vector<AssetRedirect> banks;
    unsigned fsbs{}, names{};
    for (const auto& item : index.redirects) if (item.path.starts_with(prefix) &&
        (item.path.ends_with(".fsb") || item.path.ends_with(".name"))) {
        banks.push_back(item);
        item.path.ends_with(".fsb") ? ++fsbs : ++names;
    }
    CHECK(fsbs > 0 && names > 0);
    CHECK(!fs::exists(assets / "sound/dialog" / locale));
    CHECK(MH_Initialize() == MH_OK);
    CHECK(installFileRedirect(std::move(index), logger, false));
    std::uint64_t bytes{};
    for (const auto& item : banks) {
        const auto source = wholeFile(item.target);
        const auto delivered = wholeFile(assets / item.path);
        if (source != delivered) std::printf("MISMATCH: %s\n", item.path.c_str());
        CHECK(source == delivered);
        bytes += delivered.size();
    }
    CHECK(MH_DisableHook(MH_ALL_HOOKS) == MH_OK);
    CHECK(MH_Uninitialize() == MH_OK);
    setAssetIndexForTest({});
    CHECK(!fs::exists(assets / "sound/dialog" / locale));
    fs::remove_all(root);
    std::printf("Local pack %s: %u banks, %u name tables, %llu bytes, %d failure(s); no junction/native language directory.\n",
        locale.c_str(), fsbs, names, static_cast<unsigned long long>(bytes), failures);
    return failures ? 1 : 0;
}
// Optional data-patch probe: --assets <mod-folder>. Verify every loose asset
// through both real delivery paths under a native directory that does not exist.
static int probeAssets(const fs::path& mod, const fs::path& root, fs::path gameAssets = {}, fs::path modsRoot = {}) {
    const auto assets = root / "game/assets";
    fs::create_directories(assets);
    std::vector<std::pair<std::string, std::string>> expected;
    for (const auto& entry : fs::recursive_directory_iterator(mod / "assets"))
        if (entry.is_regular_file()) expected.push_back({fs::relative(entry.path(), mod / "assets").generic_string(), wholeFile(entry.path())});
    std::vector<std::string> messages;
    std::vector<Mod> mods;
    if (modsRoot.empty()) mods.push_back({mod.filename().string(), "Asset patch probe", mod.wstring(), 0, {}});
    else mods = Index::load(modsRoot.wstring(), messages).mods;
    if (gameAssets.empty()) gameAssets = assets;
    auto index = AssetIndex::build(gameAssets.wstring(), (root / "cache").wstring(), mods, messages);
    CHECK(!expected.empty() && index.members.size() >= expected.size() && index.redirects.size() >= expected.size());
    CHECK(!fs::exists(assets / "characters"));
    CHECK(MH_Initialize() == MH_OK);
    CHECK(installFileRedirect(std::move(index), logger, false));
    std::uint64_t bytes{};
    for (const auto& [name, source] : expected) {
        std::string delivered;
        CHECK(readAssetMember(name.c_str(), delivered) && delivered == source);
        auto uppercase = name;
        for (auto& ch : uppercase) {
            if (ch >= 'a' && ch <= 'z') ch -= 32;
            if (ch == '/') ch = '\\';
        }
        CHECK(readAssetMember(uppercase.c_str(), delivered) && delivered == source);
        CHECK(wholeFile(assets / name) == source);
        bytes += source.size();
    }
    CHECK(MH_DisableHook(MH_ALL_HOOKS) == MH_OK);
    CHECK(MH_Uninitialize() == MH_OK);
    setAssetIndexForTest({});
    CHECK(!fs::exists(assets / "characters"));
    fs::remove_all(root);
    std::printf("Asset patch: %zu files, %llu bytes through engine provider and native opens, %d failure(s).\n",
        expected.size(), static_cast<unsigned long long>(bytes), failures);
    return failures ? 1 : 0;
}
int main(int argc, char** argv) {
    wchar_t temporary[MAX_PATH];
    CHECK(GetTempPathW(MAX_PATH, temporary));
    const auto root = fs::path(temporary) / (L"LiteralCrab-native-assets-" + std::to_wstring(GetCurrentProcessId()));
    if (argc == 4 && std::string_view(argv[1]) == "--pack") return probePack(fs::path(argv[2]), argv[3], root);
    if (argc == 3 && std::string_view(argv[1]) == "--assets") return probeAssets(fs::path(argv[2]), root);
    if (argc == 5 && std::string_view(argv[1]) == "--installed") return probeAssets(fs::path(argv[2]), root, fs::path(argv[3]), fs::path(argv[4]));
    const auto assets = root / "game/assets";
    const auto mods = root / "mods";
    const std::string initial = argc > 1 ? argv[1] : "RUR";
    write(assets / "sound/dialog/ENU/native.fsb", "factory");
    for (const auto* folder : {"10_first", "20_later"}) write(mods / folder / "mod.ini", "[Mod]\nName=Test pack\n");
    for (const auto* locale : {"PLP", "RUR", "ARS"}) {
        const auto relative = fs::path("sound/dialog") / locale;
        write(mods / "10_first/assets" / relative / "voice.fsb", std::string(locale) + " bank");
        write(mods / "10_first/assets" / relative / "voice.name", std::string(locale) + " names");
        write(mods / "20_later/assets" / relative / "voice.fsb", "loser");
        write(mods / "20_later/assets" / relative / "extra.fsb", "later unique");
        CHECK(!fs::exists(assets / relative));
    }
    const auto custom = fs::path("characters/visitor/new-format.custom");
    write(mods / "10_first/assets" / custom, "custom character resource");
    write(mods / "10_first/assets/characters/visitor/vanishing.custom", "vanishes");
    std::vector<std::string> messages;
    const auto enabled = Index::load(mods.wstring(), messages);
    auto index = AssetIndex::build(assets.wstring(), (mods / ".cache").wstring(), enabled.mods, messages);
    CHECK(MH_Initialize() == MH_OK);
    // Cold-start processes cover full file trace, audio-only opt-in, and the
    // release default. All three must deliver identical assets and API results.
    const bool traceFiles = initial == "PLP", traceAudio = initial == "ARS";
    CHECK(installFileRedirect(std::move(index), logger, traceFiles, traceAudio));
    CHECK(read(assets / "sound/dialog" / initial / "voice.fsb", false) == initial + " bank");
    // Switch native request locale in the same process, then repeat each locale
    // as the initial request in separate CTest processes for cold-start coverage.
    for (const auto* locale : {"PLP", "RUR", "ARS"}) {
        const auto directory = assets / "sound/dialog" / locale;
        CHECK(read(directory / "voice.fsb", false) == std::string(locale) + " bank");
        CHECK(read(directory / "voice.name", true) == std::string(locale) + " names");
        CHECK(read(directory / "extra.fsb", true) == "later unique");
        CHECK(GetFileAttributesW((directory.wstring() + L"\\").c_str()) == FILE_ATTRIBUTE_DIRECTORY);
        CHECK(GetFileAttributesA(directory.string().c_str()) == FILE_ATTRIBUTE_DIRECTORY);
        WIN32_FILE_ATTRIBUTE_DATA info{};
        CHECK(GetFileAttributesExW(directory.c_str(), GetFileExInfoStandard, &info));
        CHECK(info.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY);
        CHECK(GetFileAttributesExA(directory.string().c_str(), GetFileExInfoStandard, &info));
        CHECK(info.dwFileAttributes == FILE_ATTRIBUTE_DIRECTORY);
        CHECK(GetFileAttributesExA((directory / "voice.fsb").string().c_str(), GetFileExInfoStandard, &info));
        CHECK(info.nFileSizeLow == std::string(locale).size() + 5);
        CHECK(contains(std::string("native audio language=") + (std::string(locale) == "PLP" ? "plp" : std::string(locale) == "RUR" ? "rur" : "ars")) == (traceFiles || traceAudio));
    }
    CHECK(read(assets / custom, true) == "custom character resource");
    CHECK(contains("new-format.custom") == traceFiles);
    for (bool wide : {false, true}) {
        SetLastError(1234);
        const auto path = assets / custom;
        const auto opened = wide ? CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr)
            : CreateFileA(path.string().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
        CHECK(opened != INVALID_HANDLE_VALUE);
        CHECK(GetLastError() == ERROR_ALREADY_EXISTS);
        if (opened != INVALID_HANDLE_VALUE) CloseHandle(opened);
    }
    CHECK(GetFileAttributesA((assets / "characters/visitor").string().c_str()) == FILE_ATTRIBUTE_DIRECTORY);
    CHECK(GetFileAttributesW((assets / "characters/visito").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(GetFileAttributesW((assets / "sound/dialog/RU").c_str()) == INVALID_FILE_ATTRIBUTES);
    CHECK(read(assets / "sound/dialog/ENU/native.fsb", true) == "factory");
    CHECK(GetFileAttributesW((assets / "sound/dialog/ENU").c_str()) != INVALID_FILE_ATTRIBUTES);
    CHECK(contains("resolved=<native path>") == (traceFiles || traceAudio));
    if (!traceFiles && !traceAudio) CHECK(logs.empty());
    const auto gone = mods / "10_first/assets/characters/visitor/vanishing.custom";
    CHECK(DeleteFileW(gone.c_str()));
    const auto missing = CreateFileW((assets / "characters/visitor/vanishing.custom").c_str(), GENERIC_READ,
        FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    CHECK(missing == INVALID_HANDLE_VALUE);
    CHECK(GetLastError() == ERROR_FILE_NOT_FOUND);
    CHECK(contains("result=failed error=2"));
    // No write or creation may mutate the winning mod file through its virtual path.
    const auto writable = CreateFileW((assets / custom).c_str(), GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, 0, nullptr);
    CHECK(writable == INVALID_HANDLE_VALUE);
    CHECK(read(mods / "10_first/assets" / custom, true) == "custom character resource");
    CHECK(contains("10_first")); // Redirected failure remains visible in release.
    CHECK(contains("20_later") == (traceFiles || traceAudio));
    CHECK(MH_DisableHook(MH_ALL_HOOKS) == MH_OK);
    CHECK(MH_Uninitialize() == MH_OK);
    setAssetIndexForTest({});
    for (const auto* locale : {"PLP", "RUR", "ARS"}) CHECK(!fs::exists(assets / "sound/dialog" / locale));
    CHECK(!fs::exists(assets / "characters"));
    CHECK(read(assets / "sound/dialog/ENU/native.fsb", true) == "factory");
    // Failed installation must roll back only hooks it owns, preserving a
    // pre-existing hook from another feature/plugin.
    CHECK(MH_Initialize() == MH_OK);
    const auto kernel = GetModuleHandleW(L"kernel32.dll");
    const auto openW = reinterpret_cast<void*>(GetProcAddress(kernel, "CreateFileW"));
    const auto openA = reinterpret_cast<void*>(GetProcAddress(kernel, "CreateFileA"));
    CHECK(MH_CreateHook(openW, reinterpret_cast<void*>(&otherHook), reinterpret_cast<void**>(&otherOpen)) == MH_OK);
    AssetIndex conflict;
    conflict.redirects.push_back({"characters/visitor/resource.custom", (mods / "unused").wstring()});
    CHECK(!installFileRedirect(std::move(conflict), logger, false));
    CHECK(MH_RemoveHook(openW) == MH_OK);
    CHECK(MH_RemoveHook(openA) == MH_ERROR_NOT_CREATED);
    CHECK(MH_Uninitialize() == MH_OK);
    setAssetIndexForTest({});
    fs::remove_all(root);
    std::printf("Native asset integration: %d failure(s), initial locale %s.\n", failures, initial.c_str());
    return failures ? 1 : 0;
}
