// SPDX-License-Identifier: GPL-3.0-only
#include "mods/asset_mods.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <map>
#include <stdexcept>

namespace crabe::mods {
namespace {
    constexpr std::string_view cacheVersion = "LiteralCrab asset cache 2";
    constexpr std::uint64_t maxMemberBytes = 512ull << 20;

    std::string utf8(std::wstring_view value) {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(size, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    std::wstring windowsPath(std::string_view relative) {
        if (relative.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, relative.data(), static_cast<int>(relative.size()), nullptr, 0);
        std::wstring path(static_cast<std::size_t>((std::max)(size, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, relative.data(), static_cast<int>(relative.size()), path.data(), size);
        std::replace(path.begin(), path.end(), L'/', L'\\');
        return path;
    }
    // Lowercase, '/' separated, without "./" or leading slashes; empty when it leaves the folder.
    std::string normalizeRelative(std::string_view name) {
        std::string result;
        result.reserve(name.size());
        for (const char ch : name)
            result += ch == '\\' ? '/' : ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch;
        while (result.starts_with("./") || result.starts_with("/")) result.erase(0, result[0] == '/' ? 1 : 2);
        if (result == ".." || result.starts_with("../") || result.find("/../") != std::string::npos ||
            result.ends_with("/..") || result.find(':') != std::string::npos) return {};
        return result;
    }
    bool isFile(const std::wstring& path) {
        const auto attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }
    std::string fileStamp(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data)) throw std::runtime_error("cannot read " + utf8(path));
        return std::to_string((static_cast<std::uint64_t>(data.nFileSizeHigh) << 32) | data.nFileSizeLow) + ":" +
            std::to_string((static_cast<std::uint64_t>(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime);
    }
    void createDirectories(const std::wstring& directory) {
        for (std::size_t slash = directory.find(L'\\', 3); ; slash = directory.find(L'\\', slash + 1)) {
            const auto part = directory.substr(0, slash);
            if (!CreateDirectoryW(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS)
                throw std::runtime_error("cannot create " + utf8(part));
            if (slash == std::wstring::npos) return;
        }
    }
    bool readFile(const std::wstring& path, std::string& data) noexcept {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= maxMemberBytes;
        if (ok) {
            try {
                data.resize(static_cast<std::size_t>(size.QuadPart));
            } catch (...) {
                ok = false;
            }
        }
        DWORD done{};
        ok = ok && (data.empty() || (ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &done, nullptr) && done == data.size()));
        CloseHandle(file);
        return ok;
    }
    void writeFile(const std::wstring& path, std::string_view data) {
        const auto temporary = path + L".tmp";
        const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot write " + utf8(path));
        DWORD done{};
        const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &done, nullptr) && done == data.size();
        CloseHandle(file);
        if (!ok || !MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
            DeleteFileW(temporary.c_str());
            throw std::runtime_error("cannot write " + utf8(path));
        }
    }
    template <class Visit> void listFiles(const std::wstring& directory, const std::string& relative, const Visit& visit) {
        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileExW((directory + L"\\*").c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, 0);
        if (search == INVALID_HANDLE_VALUE) return;
        do {
            if (!wcscmp(entry.cFileName, L".") || !wcscmp(entry.cFileName, L"..")) continue;
            const auto name = relative + (relative.empty() ? "" : "/") + utf8(entry.cFileName);
            if (entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) listFiles(directory + L'\\' + entry.cFileName, name, visit);
            else visit(directory + L'\\' + entry.cFileName, name);
        } while (FindNextFileW(search, &entry));
        FindClose(search);
    }

    // One mod file below "assets": loose, or a member of a package ZIP.
    struct Source {
        std::uint16_t mod{};
        std::wstring file;
        std::shared_ptr<const zip::Archive> package;
        std::size_t entry{};
    };

    // A packaged file the game must open from disk is extracted once, then reused while the
    // package is unchanged.
    std::wstring extract(const Source& source, const std::wstring& output) {
        const auto& item = source.package->entries()[source.entry];
        const auto stamp = std::string(cacheVersion) + "\n" + utf8(source.package->path()) + "|" +
            fileStamp(source.package->path()) + "|" + item.name + "\n";
        std::string existing;
        if (!isFile(output) || !readFile(output + L".stamp", existing) || existing != stamp) {
            createDirectories(output.substr(0, output.rfind(L'\\')));
            writeFile(output, source.package->read(item));
            writeFile(output + L".stamp", stamp);
        }
        return output;
    }
}

std::string memberName(std::string_view relative) {
    // Textures are content-addressed and requested as textures/<hash>.tbody from any archive.
    if (relative.ends_with(".tbody")) {
        const auto slash = relative.rfind('/');
        return "textures/" + std::string(relative.substr(slash == std::string_view::npos ? 0 : slash + 1));
    }
    return std::string(relative);
}

std::size_t requestName(std::string_view name, char (&out)[maxAssetPath]) noexcept {
    if (name.empty() || name.size() >= maxAssetPath) return 0;
    std::size_t length = 0;
    for (const char ch : name) {
        if (static_cast<unsigned char>(ch) >= 0x80) return 0;
        out[length++] = ch == '\\' ? '/' : ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch;
    }
    return length;
}

std::size_t assetPath(std::string_view fileName, char (&out)[maxAssetPath]) noexcept {
    const auto length = requestName(fileName, out);
    if (!length) return 0;
    const std::string_view path(out, length);
    if (path.starts_with("mods/") || path.find("/mods/") != std::string_view::npos) return 0;
    std::size_t start;
    const auto inner = path.rfind("/assets/");
    if (inner != std::string_view::npos) start = inner + 8;
    else if (path.starts_with("assets/")) start = 7;
    else if (path.starts_with("./assets/")) start = 9;
    else return 0;
    if (start >= length) return 0;
    std::memmove(out, out + start, length - start);
    return length - start;
}

std::size_t assetPath(std::wstring_view fileName, char (&out)[maxAssetPath]) noexcept {
    if (fileName.size() >= maxAssetPath) return 0;
    char narrow[maxAssetPath];
    for (std::size_t index = 0; index < fileName.size(); ++index) {
        if (fileName[index] >= 0x80) return 0;
        narrow[index] = static_cast<char>(fileName[index]);
    }
    return assetPath(std::string_view(narrow, fileName.size()), out);
}

const std::wstring* AssetIndex::find(std::string_view path) const noexcept {
    const auto found = std::lower_bound(redirects.begin(), redirects.end(), path,
        [](const AssetRedirect& redirect, std::string_view value) { return redirect.path < value; });
    return found != redirects.end() && found->path == path ? &found->target : nullptr;
}

bool AssetIndex::hasDirectory(std::string_view path) const noexcept {
    if (path.empty() || path.back() == '/') return false;
    // Compare against path + '/' without allocating that temporary string. A
    // same-named file or prefix such as "rur2" is not this directory.
    const auto found = std::lower_bound(redirects.begin(), redirects.end(), path,
        [](const AssetRedirect& redirect, std::string_view value) {
            const auto order = std::string_view(redirect.path).substr(0, value.size()).compare(value);
            if (order) return order < 0;
            return redirect.path.size() == value.size() || redirect.path[value.size()] < '/';
        });
    return found != redirects.end() && found->path.size() > path.size() &&
        found->path.compare(0, path.size(), path) == 0 && found->path[path.size()] == '/';
}

const AssetMember* AssetIndex::member(std::string_view name) const noexcept {
    const auto found = std::lower_bound(members.begin(), members.end(), name,
        [](const AssetMember& item, std::string_view value) { return item.name < value; });
    return found != members.end() && found->name == name ? &*found : nullptr;
}

bool AssetIndex::read(const AssetMember& item, std::string& data) const noexcept {
    if (item.generated) {
        try { data = *item.generated; return true; } catch (...) { return false; }
    }
    if (!item.package) {
        if (!readFile(item.file, data)) return false;
    } else try {
        data = item.package->read(item.package->entries()[item.entry]);  // Verifies the CRC.
    } catch (...) {
        return false;
    }
    // Editors commonly save UTF-8 text with a BOM, which Lua 5.1 rejects.
    // Text data chunks are served without it; compiled chunks are untouched.
    const std::string_view name(item.name);
    if (data.starts_with("\xEF\xBB\xBF") && (name.ends_with(".lua") || name.ends_with(".dnax") ||
        name.ends_with(".btx") || name.ends_with(".bmx"))) data.erase(0, 3);
    return true;
}

AssetIndex AssetIndex::build(const std::wstring& gameAssets, const std::wstring& cache,
    const std::vector<Mod>& mods, std::vector<std::string>& messages) {
    AssetIndex index;
    std::map<std::string, Source> files;  // Path below assets; first mod in load order wins.
    std::vector<std::size_t> counts(mods.size());
    const auto conflict = [&](std::uint16_t existing, std::uint16_t mod, const std::string& what) {
        messages.push_back("Mod " + mods[mod].folder + ": " + what + " ignored; " +
            (existing == mod ? "the mod provides it twice." : mods[existing].folder + " already replaces it."));
    };
    const auto claim = [&](Source source, const std::string& relative) {
        if (relative.empty()) return;
        const auto [slot, created] = files.try_emplace(relative, source);
        if (!created) conflict(slot->second.mod, source.mod, "assets/" + relative);
        else ++counts[source.mod];
    };
    for (std::size_t modIndex = 0; modIndex < mods.size(); ++modIndex) {
        const auto& mod = mods[modIndex];
        const auto id = static_cast<std::uint16_t>(modIndex);
        listFiles(mod.path + L"\\assets", "", [&](const std::wstring& file, const std::string& name) {
            const auto relative = normalizeRelative(name);
            if (relative.empty()) messages.push_back("Mod " + mod.folder + ": assets/" + name + " ignored; invalid path.");
            else claim({id, file, nullptr, 0}, relative);
        });
        std::vector<std::wstring> packages;
        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileExW((mod.path + L"\\*.zip").c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, 0);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) packages.push_back(entry.cFileName);
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        std::sort(packages.begin(), packages.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        for (const auto& name : packages) {
            std::shared_ptr<const zip::Archive> package;
            try {
                package = std::make_shared<const zip::Archive>(mod.path + L'\\' + name);
            } catch (const std::exception& failure) {
                messages.push_back("Mod " + mod.folder + ": " + utf8(name) + " skipped: " + failure.what() + ".");
                continue;
            }
            std::size_t outside = 0;
            for (std::size_t member = 0; member < package->entries().size(); ++member) {
                const auto& item = package->entries()[member];
                if (item.directory()) continue;
                const auto relative = normalizeRelative(item.name);
                if (!relative.starts_with("assets/")) { ++outside; continue; }
                claim({id, {}, package, member}, relative.substr(7));
            }
            if (outside)
                messages.push_back("Mod " + mod.folder + ": " + utf8(name) + ": " + std::to_string(outside) +
                    " file(s) outside an assets folder ignored.");
        }
    }

    // Request name -> member; when two paths map to one name, the earlier mod in load order wins.
    std::map<std::string, std::pair<std::size_t, std::string>> names;
    const auto addMember = [&](std::string name, const Source& source, std::string label) {
        const auto [slot, created] = names.try_emplace(name, index.members.size(), label);
        if (created) {
            index.members.push_back({std::move(name), source.file, source.package, source.entry, source.mod});
            return;
        }
        auto& existing = index.members[slot->second.first];
        if (existing.mod <= source.mod) {
            conflict(existing.mod, source.mod, label);
            --counts[source.mod];
            return;
        }
        conflict(source.mod, existing.mod, slot->second.second);
        --counts[existing.mod];
        existing = {std::move(name), source.file, source.package, source.entry, source.mod};
        slot->second.second = std::move(label);
    };
    for (const auto& [relative, source] : files) {
        const bool onDisk = isFile(gameAssets + L'\\' + windowsPath(relative));
        try {
            if (onDisk && relative.ends_with(".zip")) {
                // A whole replacement archive, ordinary or game-made: its members are served by
                // name, so it is never converted.
                const auto archive = std::make_shared<const zip::Archive>(source.package
                    ? extract(source, cache + L"\\packages\\" + windowsPath(relative)) : source.file);
                const auto slash = relative.rfind('/');
                const auto folder = slash == std::string::npos ? std::string() : relative.substr(0, slash + 1);
                for (std::size_t member = 0; member < archive->entries().size(); ++member) {
                    const auto& item = archive->entries()[member];
                    const auto inner = normalizeRelative(item.name);
                    if (item.directory() || inner.empty()) continue;
                    if (item.method != 0 && item.method != 8)
                        throw std::runtime_error(item.name + " uses an unsupported compression method (use Deflate or Store)");
                    addMember(memberName(folder + inner), {source.mod, {}, archive, member}, "assets/" + relative + ": " + item.name);
                }
            } else {
                // Content packs add files as well as replacing them. Any of
                // these may be opened through Windows or the engine's member
                // loader, regardless of extension or native-file existence.
                const auto target = source.package ? extract(source, cache + L"\\assets\\" + windowsPath(relative)) : source.file;
                index.redirects.push_back({relative, target});
                addMember(memberName(relative), source, "assets/" + relative);
            }
        } catch (const std::exception& failure) {
            messages.push_back("Mod " + mods[source.mod].folder + ": assets/" + relative + " not applied: " + failure.what() + ".");
            --counts[source.mod];
        }
    }
    std::sort(index.redirects.begin(), index.redirects.end(), [](const AssetRedirect& a, const AssetRedirect& b) { return a.path < b.path; });
    std::sort(index.members.begin(), index.members.end(), [](const AssetMember& a, const AssetMember& b) { return a.name < b.name; });
    for (std::size_t modIndex = 0; modIndex < mods.size(); ++modIndex)
        if (counts[modIndex]) messages.push_back("Mod " + mods[modIndex].folder + ": " + std::to_string(counts[modIndex]) + " asset file(s).");
        else if (!mods[modIndex].scripts) messages.push_back("Mod " + mods[modIndex].folder + ": no scripts or usable asset files found.");
    return index;
}
}
