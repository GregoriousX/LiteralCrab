// SPDX-License-Identifier: GPL-3.0-only
#include "mods/asset_mods.hpp"
#include "mods/zip.hpp"
#include <windows.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <map>
#include <memory>
#include <stdexcept>

namespace crabe::mods {
namespace {
    constexpr std::string_view cacheVersion = "LiteralCrab asset cache 1";
    // Fixed timestamp of every member in the game's archives.
    constexpr std::uint16_t gameTime = 0x742F, gameDate = 0x99C3;
    constexpr std::uint64_t maxMemberBytes = 512ull << 20;

    std::string utf8(std::wstring_view value) {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        std::string result(static_cast<std::size_t>((std::max)(size, 0)), '\0');
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }
    std::wstring widen(std::string_view value) {
        if (value.empty()) return {};
        const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
        std::wstring result(static_cast<std::size_t>((std::max)(size, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size);
        return result;
    }
    std::wstring windowsPath(std::string_view relative) {
        auto path = widen(relative);
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
    std::string parent(std::string_view path) {
        const auto slash = path.rfind('/');
        return slash == std::string_view::npos ? std::string() : std::string(path.substr(0, slash));
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
    std::wstring directoryOf(const std::wstring& file) { return file.substr(0, file.rfind(L'\\')); }
    std::string readFile(const std::wstring& path) {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        LARGE_INTEGER size{};
        std::string data;
        if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 && static_cast<std::uint64_t>(size.QuadPart) <= maxMemberBytes) {
            data.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD done{};
            if (!data.empty() && (!ReadFile(file, data.data(), static_cast<DWORD>(data.size()), &done, nullptr) || done != data.size()))
                data.clear();
        }
        CloseHandle(file);
        return data;
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
        const zip::Archive* package{};
        const zip::Entry* entry{};
        std::string describe() const {
            return package ? utf8(package->path()) + "|" + fileStamp(package->path()) + "|" + entry->name + "|" + std::to_string(entry->crc)
                           : utf8(file) + "|" + fileStamp(file);
        }
    };
    struct Overlay {
        std::string member;
        const Source* source{};
    };
    struct ArchiveTarget {
        const Source* base{};  // Whole replacement of the archive, else the game's own.
        std::vector<Overlay> overlays;
    };

    // The game's archives, opened only where a mod file needs one and closed after the build.
    class GameArchives {
    public:
        explicit GameArchives(const std::wstring& root) : root(root) {}
        const std::vector<std::string>& zips(const std::string& directory) {
            auto [slot, created] = directories.try_emplace(directory);
            if (created) {
                WIN32_FIND_DATAW entry{};
                const auto pattern = root + (directory.empty() ? L"" : L"\\" + windowsPath(directory)) + L"\\*.zip";
                const HANDLE search = FindFirstFileExW(pattern.c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, 0);
                if (search != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) slot->second.push_back(normalizeRelative(utf8(entry.cFileName)));
                    } while (FindNextFileW(search, &entry));
                    FindClose(search);
                }
                std::sort(slot->second.begin(), slot->second.end());
            }
            return slot->second;
        }
        zip::Archive* archive(const std::string& relative) {
            auto [slot, created] = opened.try_emplace(relative);
            if (created) {
                try {
                    slot->second = std::make_unique<zip::Archive>(root + L'\\' + windowsPath(relative));
                } catch (const std::exception&) {}
            }
            return slot->second.get();
        }
    private:
        std::wstring root;
        std::map<std::string, std::vector<std::string>> directories;
        std::map<std::string, std::unique_ptr<zip::Archive>> opened;
    };

    // Finds the game archive holding relative as a member: the deepest folder first, archives in
    // name order. A file no archive holds is added to the only archive of the deepest folder that
    // has archives.
    bool locate(GameArchives& game, const std::string& relative, std::string& archive, std::string& member, std::string& error) {
        std::string fallbackArchive, fallbackMember;
        bool fallbackSeen = false;
        for (auto directory = parent(relative);; directory = parent(directory)) {
            const auto name = relative.substr(directory.empty() ? 0 : directory.size() + 1);
            const auto& zips = game.zips(directory);
            for (const auto& zipName : zips) {
                const auto archivePath = directory.empty() ? zipName : directory + "/" + zipName;
                if (const auto* opened = game.archive(archivePath))
                    if (const auto* entry = opened->find(name)) {
                        archive = archivePath;
                        member = entry->name;
                        return true;
                    }
            }
            if (!fallbackSeen && !zips.empty()) {
                fallbackSeen = true;
                if (zips.size() == 1) {
                    fallbackArchive = directory.empty() ? zips[0] : directory + "/" + zips[0];
                    fallbackMember = name;
                } else {
                    error = "assets/" + (directory.empty() ? std::string() : directory + "/") +
                        " holds several archives and none contains this file";
                }
            }
            if (directory.empty()) break;
        }
        if (!fallbackArchive.empty()) {
            archive = fallbackArchive;
            member = fallbackMember;
            return true;
        }
        if (error.empty()) error = "no game file or archive matches this path";
        return false;
    }

    // A member copied with its stored bytes; md5 is computed unless known.
    zip::Member memberFrom(const zip::Archive& archive, const zip::Entry& entry, std::string name, const zip::Md5* known) {
        if (entry.method != 0 && entry.method != 8)
            throw std::runtime_error(entry.name + " uses an unsupported compression method (use Deflate or Store)");
        zip::Member member;
        member.name = std::move(name);
        member.method = entry.method;
        member.crc = entry.crc;
        member.compressedSize = entry.compressedSize;
        member.size = entry.size;
        if (known) {
            member.md5 = *known;
        } else {
            const auto data = archive.read(entry);  // Verifies the CRC.
            member.md5 = zip::md5(data.data(), data.size());
        }
        member.write = [&archive, &entry](const auto& sink) { archive.copyRaw(entry, sink); };
        return member;
    }

    zip::Member memberFrom(const Source& source, std::string name, std::vector<std::shared_ptr<std::string>>& storage) {
        if (source.package) return memberFrom(*source.package, *source.entry, std::move(name), nullptr);
        auto data = std::make_shared<std::string>(readFile(source.file));
        if (data->empty() && !isFile(source.file)) throw std::runtime_error("cannot read " + utf8(source.file));
        storage.push_back(data);
        zip::Member member;
        member.name = std::move(name);
        member.method = 0;
        member.crc = zip::crc32(data->data(), data->size());
        member.compressedSize = member.size = static_cast<std::uint32_t>(data->size());
        member.md5 = zip::md5(data->data(), data->size());
        member.write = [data](const auto& sink) { sink(data->data(), data->size()); };
        return member;
    }

    bool sameMember(std::string_view left, std::string_view right) {
        return normalizeRelative(left) == normalizeRelative(right);
    }

    void buildArchive(GameArchives& game, const std::string& relative,
        const ArchiveTarget& target, const std::wstring& output) {
        std::unique_ptr<zip::Archive> replacement;
        std::wstring extracted;
        const zip::Archive* base = nullptr;
        if (target.base && target.base->package) {
            extracted = output + L".base";
            writeFile(extracted, target.base->package->read(*target.base->entry));
            replacement = std::make_unique<zip::Archive>(extracted);
            base = replacement.get();
        } else if (target.base) {
            replacement = std::make_unique<zip::Archive>(target.base->file);
            base = replacement.get();
        } else {
            base = game.archive(relative);
            if (!base) throw std::runtime_error("cannot read the game archive");
        }
        std::vector<zip::Member> members;
        std::vector<std::shared_ptr<std::string>> storage;
        std::vector<bool> used(target.overlays.size());
        for (const auto& entry : base->entries()) {
            if (entry.directory()) continue;
            auto overlay = std::find_if(target.overlays.begin(), target.overlays.end(),
                [&](const Overlay& item) { return sameMember(item.member, entry.name); });
            if (overlay == target.overlays.end()) {
                members.push_back(memberFrom(*base, entry, entry.name, entry.hasMd5 ? &entry.md5 : nullptr));
            } else {
                used[static_cast<std::size_t>(overlay - target.overlays.begin())] = true;
                members.push_back(memberFrom(*overlay->source, entry.name, storage));
            }
        }
        for (std::size_t index = 0; index < target.overlays.size(); ++index)
            if (!used[index]) members.push_back(memberFrom(*target.overlays[index].source, target.overlays[index].member, storage));
        const auto& entries = base->entries();
        const auto first = std::find_if(entries.begin(), entries.end(), [](const zip::Entry& entry) { return !entry.directory(); });
        const bool gameLayout = first != entries.end() && first->hasMd5;
        zip::writeGameArchive(output, members, gameLayout ? first->time : gameTime, gameLayout ? first->date : gameDate);
        replacement.reset();
        if (!extracted.empty()) DeleteFileW(extracted.c_str());
    }

    bool inGameLayout(const std::wstring& path) {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        char signature[4]{};
        DWORD done{};
        const bool ok = ReadFile(file, signature, 4, &done, nullptr) && done == 4 && !std::memcmp(signature, "PK\x05\x06", 4);
        CloseHandle(file);
        return ok;
    }
}

std::size_t assetPath(std::string_view fileName, char (&out)[maxAssetPath]) noexcept {
    if (fileName.size() >= maxAssetPath) return 0;
    std::size_t length = 0;
    for (const char ch : fileName) {
        if (static_cast<unsigned char>(ch) >= 0x80) return 0;
        out[length++] = ch == '\\' ? '/' : ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch;
    }
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

AssetIndex AssetIndex::build(const std::wstring& gameAssets, const std::wstring& cache,
    const std::vector<Mod>& mods, std::vector<std::string>& messages) {
    AssetIndex index;
    std::vector<std::unique_ptr<zip::Archive>> packages;
    std::vector<std::unique_ptr<Source>> sources;
    std::map<std::string, const Source*> claims;  // First mod in load order wins.
    std::vector<std::size_t> counts(mods.size());
    const auto claim = [&](std::unique_ptr<Source> source, std::string relative) {
        const auto& mod = mods[source->mod];
        if (relative.empty() || relative.back() == '/') return;
        const auto [slot, created] = claims.try_emplace(relative, source.get());
        if (!created) {
            messages.push_back("Mod " + mod.folder + ": assets/" + relative + " ignored; " +
                (slot->second->mod == source->mod ? "the mod provides it twice." : mods[slot->second->mod].folder + " already replaces it."));
            return;
        }
        ++counts[source->mod];
        sources.push_back(std::move(source));
    };
    for (std::size_t modIndex = 0; modIndex < mods.size(); ++modIndex) {
        const auto& mod = mods[modIndex];
        const auto id = static_cast<std::uint16_t>(modIndex);
        listFiles(mod.path + L"\\assets", "", [&](const std::wstring& file, const std::string& name) {
            auto source = std::make_unique<Source>();
            source->mod = id;
            source->file = file;
            const auto relative = normalizeRelative(name);
            if (relative.empty()) messages.push_back("Mod " + mod.folder + ": assets/" + name + " ignored; invalid path.");
            else claim(std::move(source), relative);
        });
        std::vector<std::wstring> archives;
        WIN32_FIND_DATAW entry{};
        const HANDLE search = FindFirstFileExW((mod.path + L"\\*.zip").c_str(), FindExInfoBasic, &entry, FindExSearchNameMatch, nullptr, 0);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) archives.push_back(entry.cFileName);
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        std::sort(archives.begin(), archives.end(), [](const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; });
        for (const auto& name : archives) {
            try {
                packages.push_back(std::make_unique<zip::Archive>(mod.path + L'\\' + name));
            } catch (const std::exception& failure) {
                messages.push_back("Mod " + mod.folder + ": " + utf8(name) + " skipped: " + failure.what() + ".");
                continue;
            }
            const auto& package = *packages.back();
            std::size_t outside = 0;
            for (const auto& member : package.entries()) {
                if (member.directory()) continue;
                const auto relative = normalizeRelative(member.name);
                if (!relative.starts_with("assets/")) { ++outside; continue; }
                auto source = std::make_unique<Source>();
                source->mod = id;
                source->package = &package;
                source->entry = &member;
                claim(std::move(source), relative.substr(7));
            }
            if (outside)
                messages.push_back("Mod " + mod.folder + ": " + utf8(name) + ": " + std::to_string(outside) +
                    " file(s) outside an assets folder ignored.");
        }
    }

    GameArchives game(gameAssets);
    std::map<std::string, ArchiveTarget> archives;
    std::map<std::string, const Source*> files;
    for (const auto& [relative, source] : claims) {
        const auto& mod = mods[source->mod];
        if (isFile(gameAssets + L'\\' + windowsPath(relative))) {
            if (relative.ends_with(".zip")) archives[relative].base = source;
            else files.emplace(relative, source);
            continue;
        }
        std::string archive, member, error;
        if (!locate(game, relative, archive, member, error)) {
            messages.push_back("Mod " + mod.folder + ": assets/" + relative + " ignored; " + error + ".");
            --counts[source->mod];
            continue;
        }
        archives[archive].overlays.push_back({member, source});
    }

    for (const auto& [relative, source] : files) {
        if (!source->package) {
            index.redirects.push_back({relative, source->file});
            continue;
        }
        const auto output = cache + L"\\assets\\" + windowsPath(relative);
        try {
            const auto stamp = std::string(cacheVersion) + "\n" + source->describe() + "\n";
            if (!isFile(output) || readFile(output + L".stamp") != stamp) {
                createDirectories(directoryOf(output));
                writeFile(output, source->package->read(*source->entry));
                writeFile(output + L".stamp", stamp);
            }
            index.redirects.push_back({relative, output});
        } catch (const std::exception& failure) {
            messages.push_back("Mod " + mods[source->mod].folder + ": assets/" + relative + " not applied: " + failure.what() + ".");
        }
    }

    for (auto& [relative, target] : archives) {
        // A replacement already in the game's layout needs no rebuild.
        if (target.base && !target.base->package && target.overlays.empty() && inGameLayout(target.base->file)) {
            index.redirects.push_back({relative, target.base->file});
            continue;
        }
        const auto output = cache + L"\\assets\\" + windowsPath(relative);
        try {
            std::string stamp = std::string(cacheVersion) + "\nbase=" +
                (target.base ? target.base->describe() : utf8(gameAssets + L'\\' + windowsPath(relative)) + "|" +
                    fileStamp(gameAssets + L'\\' + windowsPath(relative))) + "\n";
            std::sort(target.overlays.begin(), target.overlays.end(),
                [](const Overlay& a, const Overlay& b) { return a.member < b.member; });
            for (const auto& overlay : target.overlays) stamp += overlay.member + "=" + overlay.source->describe() + "\n";
            if (!isFile(output) || readFile(output + L".stamp") != stamp) {
                createDirectories(directoryOf(output));
                DeleteFileW((output + L".stamp").c_str());
                buildArchive(game, relative, target, output);
                writeFile(output + L".stamp", stamp);
            }
            index.redirects.push_back({relative, output});
        } catch (const std::exception& failure) {
            messages.push_back("Archive assets/" + relative + " not rebuilt: " + failure.what() + "; the game's original is used.");
        }
    }
    std::sort(index.redirects.begin(), index.redirects.end(),
        [](const AssetRedirect& a, const AssetRedirect& b) { return a.path < b.path; });
    for (std::size_t modIndex = 0; modIndex < mods.size(); ++modIndex)
        if (counts[modIndex]) messages.push_back("Mod " + mods[modIndex].folder + ": " + std::to_string(counts[modIndex]) + " asset file(s).");
        else if (!mods[modIndex].scripts) messages.push_back("Mod " + mods[modIndex].folder + ": no scripts or usable asset files found.");
    return index;
}
}
