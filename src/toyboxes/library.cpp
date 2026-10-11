// SPDX-License-Identifier: GPL-3.0-only
#include "toyboxes/library.hpp"
#include "mods/zip.hpp"
#include "toyboxes/save_file.hpp"
#include <windows.h>
#include <algorithm>
#include <cwchar>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string_view>
#include <unordered_set>

namespace crabe::toyboxes {
namespace {
    // Bounds for untrusted packages; the game's own Toy Boxes are far smaller.
    // A Toy Box's files hold well under 1 MB each; a package of a hundred is a few MB.
    constexpr std::size_t maxEntries = 4096, maxMember = 4u << 20;
    constexpr std::uint64_t maxPackage = 128ull << 20;  // Uncompressed members read from one package.
    // Bumped whenever conversions change, so cached Toy Boxes are converted again.
    constexpr char cacheMagic[] = "LiteralCrab Toy Box cache 5";
    const wchar_t* const extensions[3] = {L".u", L".uh", L".uc"};

    std::uint64_t fnv(const void* data, std::size_t size, std::uint64_t hash = 0xCBF29CE484222325ull) noexcept {
        const auto* bytes = static_cast<const unsigned char*>(data);
        for (std::size_t index = 0; index < size; ++index) hash = (hash ^ bytes[index]) * 0x100000001B3ull;
        return hash;
    }
    std::wstring hex(std::uint64_t value) {
        wchar_t text[17]{};
        swprintf_s(text, L"%016llx", static_cast<unsigned long long>(value));
        return text;
    }
    std::string narrow(const std::wstring& text) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
        std::string out(size > 0 ? size : 0, '\0');
        if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), out.data(), size, nullptr, nullptr);
        return out;
    }
    bool lessPath(const std::wstring& a, const std::wstring& b) { return _wcsicmp(a.c_str(), b.c_str()) < 0; }

    // A ZIP file, or a folder's loose save files (path is then the folder).
    struct Package {
        std::wstring path;
        std::uint64_t size{}, time{};  // For a folder: the loose files' total size and a hash of their state.
        bool online{};
        std::vector<std::wstring> loose;
    };
    // A loose file is used when it starts like a save file (the 0x207 header, either byte order).
    bool looseSave(const std::wstring& path, std::uint64_t size) {
        if (size < 0x54 || size > maxMember) return false;
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        unsigned char head[4]{};
        DWORD read{};
        const bool ok = ReadFile(file, head, 4, &read, nullptr) && read == 4;
        CloseHandle(file);
        return ok && ((head[0] == 7 && head[1] == 2 && !head[2] && !head[3]) || (!head[0] && !head[1] && head[2] == 2 && head[3] == 7));
    }
    void findPackages(const std::wstring& folder, const std::wstring& skip, bool online, int depth, std::vector<Package>& out) {
        if (depth > 16 || !_wcsicmp(folder.c_str(), skip.c_str())) return;
        WIN32_FIND_DATAW item{};
        const HANDLE find = FindFirstFileExW((folder + L"\\*").c_str(), FindExInfoBasic, &item, FindExSearchNameMatch, nullptr, 0);
        if (find == INVALID_HANDLE_VALUE) return;
        std::vector<std::wstring> folders;
        const auto first = out.size();
        Package loose{folder, 0, 0xCBF29CE484222325ull, online, {}};
        do {
            const std::wstring name = item.cFileName;
            if (name == L"." || name == L".." || (item.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)) continue;
            if (item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(item.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) folders.push_back(folder + L"\\" + name);
            } else if (name.size() > 4 && !_wcsicmp(name.c_str() + name.size() - 4, L".zip")) {
                const auto size = static_cast<std::uint64_t>(item.nFileSizeHigh) << 32 | item.nFileSizeLow;
                const auto time = static_cast<std::uint64_t>(item.ftLastWriteTime.dwHighDateTime) << 32 | item.ftLastWriteTime.dwLowDateTime;
                out.push_back({folder + L"\\" + name, size, time, online, {}});
            } else {
                const auto size = static_cast<std::uint64_t>(item.nFileSizeHigh) << 32 | item.nFileSizeLow;
                if (looseSave(folder + L"\\" + name, size)) {
                    loose.loose.push_back(name);
                    loose.size += size;
                    loose.time = fnv(name.data(), name.size() * sizeof(wchar_t), loose.time);
                    loose.time = fnv(&size, sizeof(size), loose.time);
                    loose.time = fnv(&item.ftLastWriteTime, sizeof(item.ftLastWriteTime), loose.time);
                }
            }
        } while (FindNextFileW(find, &item));
        FindClose(find);
        if (!loose.loose.empty()) {
            std::sort(loose.loose.begin(), loose.loose.end(), lessPath);
            out.push_back(std::move(loose));
        }
        std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
            [](const Package& a, const Package& b) { return lessPath(a.path, b.path); });
        std::sort(folders.begin(), folders.end(), lessPath);
        for (const auto& child : folders) findPackages(child, skip, online, depth + 1, out);
    }

    bool writeFile(const std::wstring& path, std::string_view data) {
        const auto temporary = path + L".tmp";
        const HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written{};
        const bool ok = WriteFile(file, data.data(), static_cast<DWORD>(data.size()), &written, nullptr) && written == data.size();
        CloseHandle(file);
        if (ok && MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        DeleteFileW(temporary.c_str());
        return false;
    }
    bool readText(const std::wstring& path, std::string& out) {
        const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER size{};
        bool ok = GetFileSizeEx(file, &size) && size.QuadPart < (1 << 24);
        if (ok) {
            out.resize(static_cast<std::size_t>(size.QuadPart));
            DWORD read{};
            ok = ReadFile(file, out.data(), static_cast<DWORD>(out.size()), &read, nullptr) && read == out.size();
        }
        CloseHandle(file);
        return ok;
    }
    std::uint32_t fileSize(const std::wstring& path) {
        WIN32_FILE_ATTRIBUTE_DATA data{};
        if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &data) || data.nFileSizeHigh) return 0;
        return data.nFileSizeLow;
    }

    // One Toy Box being assembled from a package's members.
    struct Parts {
        std::string data, header, screen;  // Texts.
        Kind dataKind{}, headerKind{}, screenKind{};
        Cmp1 dataBlock, screenBlock;       // Original blocks, reused when no conversion is needed.
    };
    // "folder/SRR12" -> "folder/#12"; names without a number keep their own group.
    std::string groupOf(const std::string& member) {
        const auto slash = member.find_last_of('/');
        const auto folder = slash == std::string::npos ? std::string() : member.substr(0, slash + 1);
        auto base = member.substr(folder.size());
        if (const auto dot = base.find('.'); dot != std::string::npos && dot > 0) base.resize(dot);
        std::size_t digits = base.size();
        while (digits && base[digits - 1] >= '0' && base[digits - 1] <= '9') --digits;
        return digits < base.size() ? folder + "#" + base.substr(digits) : folder + base;
    }
    std::string fallbackName(const std::wstring& package, const std::string& group) {
        auto stem = package.substr(package.find_last_of(L'\\') + 1);
        if (stem.size() > 4 && !_wcsicmp(stem.c_str() + stem.size() - 4, L".zip")) stem.resize(stem.size() - 4);
        const auto hash = group.find('#');
        return narrow(stem) + (hash == std::string::npos ? std::string() : " " + group.substr(hash + 1));
    }

    struct Converted {
        std::string name;
        std::uint64_t signature{};
        std::string files[3];
    };
    Converted convert(Parts& parts, const std::wstring& package, const std::string& group, std::string_view target,
        WorldCheck worldExists) {
        if (parts.dataKind == Kind::Unknown) throw std::runtime_error("no Toy Box data");
        Converted out;
        std::string data;
        Cmp1 dataBlock;
        if (parts.dataKind == Kind::UgcData) {
            data = std::move(parts.data);
            dataBlock = std::move(parts.dataBlock);
        } else {
            data = ugcDataFromSlot(parts.data);
            dataBlock = encode(data);
        }
        validateData(data);
        const auto place = world(data);
        // Another player's Toy Box Launch, Hall of Heroes or other hub progress is not a Toy Box.
        if (!toyBoxWorld(place)) throw std::runtime_error("not a Toy Box (" + (place.empty() ? std::string("no world") : place) + ")");
        if (!worldExists(place)) throw std::runtime_error("world " + place + " is not installed");
        // The header is always written anew from checked, length-limited fields: a package's own
        // header text never reaches the game.
        const bool ugc = parts.headerKind == Kind::UgcHeader;
        const auto& source = parts.header;  // Empty when the package has data only.
        auto name = field(source, ugc ? "AUTHOREDNAME" : "USERNAME");
        auto description = field(source, ugc ? "AUTHOREDDESC" : "DESCRIPTION");
        const auto date = field(source, ugc ? "DATESTRING" : "DATESTAMP");
        // Saves named by the game carry a translation key ("@Scn_Save_Autosave"), which Toy Box
        // Share shows as it is: they get a readable name, and the package they came from.
        if (name.empty() || name[0] == '@') {
            const bool autosave = name.find("Autosave") != std::string::npos;
            name = autosave ? (date.empty() ? std::string("Autosave") : "Autosave " + date) : fallbackName(package, group);
            if (description.empty()) description = fallbackName(package, std::string());
        }
        auto level = field(source, "LASTLEVEL");
        if (_stricmp(level.c_str(), place.c_str())) level = place;  // Only the Toy Box's own world.
        const auto header = ugcHeader(data, name, description, field(source, ugc ? "TIMESTRING" : "TIMESTAMP"), date,
            field(source, ugc ? "TIMESTAMP" : "TIME"), level, target);
        out.name = field(header, "AUTHOREDNAME");
        out.signature = fnv(data.data(), data.size());
        out.files[0] = pcFile(dataBlock);
        out.files[1] = pcFile(encode(header));
        // A screenshot that does not match its declared size is left out: the menu shows its
        // default picture instead.
        if (parts.screenKind != Kind::Unknown && validScreen(parts.screen))
            out.files[2] = parts.screenKind == Kind::UgcScreen ? pcFile(parts.screenBlock) : pcFile(encode(ugcScreen(parts.screen)));
        return out;
    }

    // Reads, checks and converts one package; returns the cache list it wrote.
    std::string convertPackage(const Package& package, const std::wstring& cache, const std::wstring& key,
        WorldCheck worldExists, LogFunction log) {
        // The members of a ZIP, or a folder's loose save files.
        struct Member {
            std::string name;
            std::uint64_t size{};
            std::function<std::string()> read;
        };
        std::vector<Member> members;
        std::unique_ptr<mods::zip::Archive> archive;
        if (package.loose.empty()) {
            archive = std::make_unique<mods::zip::Archive>(package.path);
            for (const auto& entry : archive->entries())
                if (!entry.directory()) members.push_back({entry.name, entry.size, [&archive, &entry] { return archive->read(entry); }});
        } else {
            for (const auto& name : package.loose) {
                const auto path = package.path + L"\\" + name;
                members.push_back({narrow(name), 0, [path] {
                    std::string data;
                    if (!readText(path, data) || data.size() > maxMember) throw std::runtime_error("cannot read the file");
                    return data;
                }});
            }
        }
        if (members.size() > maxEntries) throw std::runtime_error("too many files");
        std::map<std::string, Parts> groups;
        std::uint64_t total = 0;
        for (const auto& entry : members) {
            if (entry.size > maxMember || (total += entry.size) > maxPackage) {
                log("Toy Boxes: %s: %s skipped (too large).", narrow(package.path).c_str(), entry.name.c_str());
                continue;
            }
            try {
                const auto block = decodeFile(entry.read());
                auto text = toyboxes::inflate(block);
                const auto kind = classify(text);
                auto& parts = groups[groupOf(entry.name)];
                switch (kind) {
                case Kind::UgcData: case Kind::SlotData:
                    parts.data = std::move(text); parts.dataKind = kind; parts.dataBlock = block; break;
                case Kind::UgcHeader: case Kind::SlotHeader:
                    parts.header = std::move(text); parts.headerKind = kind; break;
                case Kind::UgcScreen: case Kind::SlotScreen:
                    parts.screen = std::move(text); parts.screenKind = kind; parts.screenBlock = block; break;
                default: break;  // Profiles, help states and play set saves without a Toy Box.
                }
            } catch (const std::exception& failure) {
                // Not every member is a Toy Box file (readme, pictures): log only real save files.
                if (entry.size >= 0x54) log("Toy Boxes: %s: %s skipped (%s).", narrow(package.path).c_str(), entry.name.c_str(), failure.what());
            }
        }
        // A folder with a single Toy Box needs no numbers: its header and screenshot belong to it
        // whatever they are called.
        std::map<std::string, std::vector<Parts*>> folders;
        for (auto& [group, parts] : groups) folders[group.substr(0, group.find_last_of('/') + 1)].push_back(&parts);
        for (auto& [folder, list] : folders) {
            Parts* only{};
            std::size_t withData = 0;
            for (auto* parts : list) if (parts->dataKind != Kind::Unknown) { only = parts; ++withData; }
            if (withData != 1) continue;
            for (auto* parts : list) {
                if (parts == only) continue;
                if (only->headerKind == Kind::Unknown && parts->headerKind != Kind::Unknown) {
                    only->header = std::move(parts->header); only->headerKind = parts->headerKind; parts->headerKind = Kind::Unknown;
                }
                if (only->screenKind == Kind::Unknown && parts->screenKind != Kind::Unknown) {
                    only->screen = std::move(parts->screen); only->screenKind = parts->screenKind;
                    only->screenBlock = std::move(parts->screenBlock); parts->screenKind = Kind::Unknown;
                }
            }
        }
        std::string list = std::string(cacheMagic) + "\n";
        unsigned number = 0;
        for (auto& [group, parts] : groups) {
            if (parts.dataKind == Kind::Unknown) continue;
            try {
                const auto converted = convert(parts, package.path, group, package.online ? publicTarget : privateTarget,
                    worldExists);
                const auto base = cache + L"\\" + key + L"_" + std::to_wstring(number);
                for (int file = 0; file < 3; ++file)
                    if (!converted.files[file].empty() && !writeFile(base + extensions[file], converted.files[file]))
                        throw std::runtime_error("cannot write the cache");
                list += std::to_string(number) + "\t" + std::to_string(converted.signature) + "\t" + converted.name + "\n";
                ++number;
            } catch (const std::exception& failure) {
                log("Toy Boxes: %s: %s skipped (%s).", narrow(package.path).c_str(), group.c_str(), failure.what());
            }
        }
        return list;
    }
}

std::vector<Toybox> buildLibrary(const std::vector<Source>& sources, const std::wstring& cache,
    WorldCheck worldExists, LogFunction log, std::size_t maxToyboxes) {
    std::vector<Toybox> library;
    CreateDirectoryW(cache.c_str(), nullptr);
    std::vector<Package> packages;
    for (const auto& source : sources) findPackages(source.folder, cache, source.online, 0, packages);
    std::set<std::wstring> keys;
    std::unordered_set<std::uint64_t> seen;
    unsigned converted = 0, duplicates = 0;
    for (const auto& package : packages) {
        if (library.size() >= maxToyboxes) {
            log("Toy Boxes: the limit of %zu Toy Boxes is reached; later packages are not listed.", maxToyboxes);
            break;
        }
        auto lower = package.path;
        CharLowerBuffW(lower.data(), static_cast<DWORD>(lower.size()));
        auto hash = fnv(lower.data(), lower.size() * sizeof(wchar_t));
        hash = fnv(&package.size, sizeof(package.size), hash);
        hash = fnv(&package.online, sizeof(package.online), hash);  // The tab is part of the conversion.
        const auto key = hex(fnv(&package.time, sizeof(package.time), hash));
        keys.insert(key);
        const auto listPath = cache + L"\\" + key + L".lst";
        std::string list;
        if (!readText(listPath, list) || list.rfind(cacheMagic, 0) != 0) {
            try {
                list = convertPackage(package, cache, key, worldExists, log);
                if (!writeFile(listPath, list)) log("Toy Boxes: cannot write the cache list for %s.", narrow(package.path).c_str());
                ++converted;
            } catch (const std::exception& failure) {
                log("Toy Boxes: %s skipped (%s).", narrow(package.path).c_str(), failure.what());
                continue;
            }
        }
        // Lines after the magic: number, signature, name.
        for (std::size_t start = list.find('\n') + 1; start < list.size();) {
            auto end = list.find('\n', start);
            if (end == std::string::npos) end = list.size();
            const auto line = std::string_view(list).substr(start, end - start);
            start = end + 1;
            const auto tab1 = line.find('\t'), tab2 = tab1 == std::string_view::npos ? tab1 : line.find('\t', tab1 + 1);
            if (tab2 == std::string_view::npos) continue;
            Toybox toybox;
            toybox.online = package.online;
            toybox.name = std::string(line.substr(tab2 + 1));
            toybox.signature = std::strtoull(std::string(line.substr(tab1 + 1, tab2 - tab1 - 1)).c_str(), nullptr, 10);
            const auto base = cache + L"\\" + key + L"_" + std::wstring(line.begin(), line.begin() + tab1);
            for (int file = 0; file < 3; ++file) {
                toybox.files[file] = base + extensions[file];
                toybox.sizes[file] = fileSize(toybox.files[file]);
                if (!toybox.sizes[file]) toybox.files[file].clear();
            }
            if (!toybox.sizes[0] || !toybox.sizes[1]) {
                DeleteFileW(listPath.c_str());  // Converted again next time.
                continue;
            }
            if (!seen.insert(toybox.signature).second) { ++duplicates; continue; }
            library.push_back(std::move(toybox));
        }
    }
    // Remove cache files of packages that are gone or changed.
    WIN32_FIND_DATAW item{};
    const HANDLE find = FindFirstFileExW((cache + L"\\*").c_str(), FindExInfoBasic, &item, FindExSearchNameMatch, nullptr, 0);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            const std::wstring name = item.cFileName;
            if ((item.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || name.size() < 17) continue;
            if (!keys.count(name.substr(0, 16))) DeleteFileW((cache + L"\\" + name).c_str());
        } while (FindNextFileW(find, &item));
        FindClose(find);
    }
    log("Toy Boxes: %zu from %zu package(s); %u package(s) converted, %u duplicate(s) skipped.",
        library.size(), packages.size(), converted, duplicates);
    return library;
}
}
