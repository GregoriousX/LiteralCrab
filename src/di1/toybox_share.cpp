// SPDX-License-Identifier: GPL-3.0-only
// [ToyBoxes] Enabled: Toy Box Share with Toy Boxes from the toyboxes folder and web servers.
//
// The PC game keeps the whole Toy Box Share menu of the original release (UGCTarget, UGCPublic,
// UGCPrivate) and its native backend, which lists, shows and launches Toy Boxes stored as save
// files U<n>, UH<n> and UC<n> (data, header, screenshot) without any server. Gold hides the menu:
// its "To Go" build reports UI_IsTOGOOfflineGame() and the Toy Box menu leaves the entry out. On
// the Wii U the same menu sits in the Toy Box menu as well.
//
// The Toy Boxes of the packages become additional, read-only Toy Box Share slots: those of the
// toyboxes folder under "My Toy Boxes", those mirrored from servers under "Disney's Toy Boxes"
// (the catalogue that came from Disney's servers). Nothing is written to the save folder:
//   - The save system lists SavedGame\win32 once at startup and after each write or delete
//     (0x48F7E0, FindFirstFileA/FindNextFileA); the virtual slots are appended to that listing.
//   - Reads of those names (CreateFileA, GetFileAttributes*) open the converted files in the
//     cache; writes to them are refused and deleting one hides it for the session.
//   - The slot count (0xBCE110, 101) grows by the number of Toy Boxes; the virtual slots follow
//     the game's own, so downloads and uploads keep using slots 0..100.
// Only the game executable's imports are redirected, so asset mods' kernel32 hooks still apply.
// The work at startup runs on a background thread; the game waits for it only at its first
// listing of the save folder, and the hooks do one prefix comparison per call otherwise.
#include "di1/toybox_share.hpp"
#include "di1/imports.hpp"
#include "toyboxes/library.hpp"
#include "toyboxes/server.hpp"
#include <atomic>
#include <cstring>
#include <cwchar>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace crabe::di1 {
const std::string_view toyBoxShareMenuPatch = R"lua(
local menu, _, log = ...
if type(menu) ~= "table" or type(menu.GetItems) ~= "function" then
    log("FALLBACK: Toy Box menu not found; Toy Box Share has no entry")
    return
end
if menu.literalCrabToyBoxShare then return end
menu.literalCrabToyBoxShare = true
local getItems = menu.GetItems
menu.GetItems = function(self, ...)
    local items = getItems(self, ...)
    if type(items) == "string" and not string.find(items, "Scn_UGC_Title", 1, true) then
        items = items == "" and "Scn_UGC_Title" or items .. ",Scn_UGC_Title"
    end
    return items
end
log("APPLIED: Toy Box Share entry in the Toy Box menu")
)lua";

namespace {
    // The game's own Toy Box Share slots are 0..100 (0x65 at 0xBCE110).
    constexpr std::uint32_t slotLimitRva = 0x7CE110, firstVirtualSlot = 0x65;
    constexpr const char* slotLimitCode = "B8 65 00 00 00 C3";
    constexpr const char* prefixes[3] = {"U", "UH", "UC"};

    using Library = std::vector<toyboxes::Toybox>;
    bool active{};
    std::uint32_t* slotLimit{};
    SRWLOCK libraryLock = SRWLOCK_INIT;
    std::shared_ptr<const Library> library;  // Published under libraryLock; slots never move.
    std::vector<std::uint64_t> hidden;       // Signatures deleted in this session.
    HANDLE ready{}, serversDone{};
    // How long the game's first listing of the save folder waits for the servers at startup.
    constexpr DWORD serverWait = 5000;
    std::atomic<bool> saveFolderKnown{};
    char saveFolder[MAX_PATH]{};             // "...\savedgame\win32\" as the game passes it.
    std::size_t saveFolderLength{};
    std::wstring gameFolder, cacheFolder;
    std::vector<std::wstring> servers;
    toyboxes::ServerLimits serverLimits;
    std::size_t maximum = 5000;

    ImportPatch findFirstImport, findNextImport, findCloseImport, createFileImport, attributesImport,
        attributesExImport, deleteImport;

    std::shared_ptr<const Library> current() {
        AcquireSRWLockShared(&libraryLock);
        auto value = library;
        ReleaseSRWLockShared(&libraryLock);
        return value;
    }
    bool isHidden(std::uint64_t signature) {
        AcquireSRWLockShared(&libraryLock);
        bool found = false;
        for (const auto value : hidden) found = found || value == signature;
        ReleaseSRWLockShared(&libraryLock);
        return found;
    }
    void setSlotLimit(std::uint32_t count) {
        DWORD old{};
        if (!slotLimit || !VirtualProtect(slotLimit, 4, PAGE_EXECUTE_READWRITE, &old)) return;
        InterlockedExchange(reinterpret_cast<volatile LONG*>(slotLimit), static_cast<LONG>(firstVirtualSlot + count));
        VirtualProtect(slotLimit, 4, old, &old);
        FlushInstructionCache(GetCurrentProcess(), slotLimit, 4);
    }
    // Slots never move during a session: a Toy Box keeps its slot when its package is renamed,
    // moved or mirrored again (it then points at the new cache files), one whose package is gone
    // is hidden, and new Toy Boxes are appended.
    void publish(Library fresh) {
        auto merged = std::make_shared<Library>();
        if (const auto old = current()) *merged = *old;
        std::vector<bool> placed(fresh.size());
        std::vector<std::uint64_t> gone;
        for (auto& existing : *merged) {
            bool found = false;
            for (std::size_t item = 0; !found && item < fresh.size(); ++item)
                if (!placed[item] && fresh[item].signature == existing.signature) {
                    existing = std::move(fresh[item]);
                    placed[item] = found = true;
                }
            if (!found) gone.push_back(existing.signature);
        }
        std::size_t added = 0;
        for (std::size_t item = 0; item < fresh.size(); ++item)
            if (!placed[item]) { merged->push_back(std::move(fresh[item])); ++added; }
        const auto count = merged->size();
        AcquireSRWLockExclusive(&libraryLock);
        library = std::move(merged);
        hidden.insert(hidden.end(), gone.begin(), gone.end());
        ReleaseSRWLockExclusive(&libraryLock);
        setSlotLimit(static_cast<std::uint32_t>(count));
        if (!gone.empty()) log("Toy Box Share: %zu Toy Box(es) no longer available; hidden.", gone.size());
        if (added) {
            std::size_t online = 0;
            for (const auto& toybox : *current()) online += toybox.online;
            log("Toy Box Share: %zu Toy Box(es) in My Toy Boxes, %zu in Disney's Toy Boxes (slots %u..%zu).", count - online,
                online, firstVirtualSlot, firstVirtualSlot + count - 1);
        }
    }

    bool worldInstalled(const std::string& world) {
        if (world.empty() || world.size() > 64 || world.find_first_of("\\/.:") != std::string::npos) return false;
        const auto path = gameFolder + L"assets\\worlds\\" + std::wstring(world.begin(), world.end()) + L".zip";
        return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
    }
    std::vector<toyboxes::Source> roots() { return {{gameFolder + L"toyboxes", false}, {cacheFolder + L"\\servers", true}}; }
    DWORD WINAPI worker(void*) {
        publish(toyboxes::buildLibrary(roots(), cacheFolder, &worldInstalled, &log, maximum));
        SetEvent(ready);
        // The first listing waits briefly for the servers. Packages that arrive later appear at the
        // game's next listing, after any save, or at the next start.
        CreateDirectoryW((cacheFolder + L"\\servers").c_str(), nullptr);
        if (toyboxes::syncServers(servers, cacheFolder + L"\\servers", &log, serverLimits))
            publish(toyboxes::buildLibrary(roots(), cacheFolder, &worldInstalled, &log, maximum));
        SetEvent(serversDone);
        return 0;
    }

    // A virtual slot file: its library entry and file index, or null.
    struct Virtual {
        std::shared_ptr<const Library> library;
        const toyboxes::Toybox* toybox{};
        int file{};
    };
    bool parseName(const char* name, Virtual& out) {
        int file;
        const char* digits;
        if ((name[0] | 0x20) != 'u') return false;
        if ((name[1] | 0x20) == 'h') { file = 1; digits = name + 2; }
        else if ((name[1] | 0x20) == 'c') { file = 2; digits = name + 2; }
        else { file = 0; digits = name + 1; }
        if (*digits < '0' || *digits > '9') return false;
        char* end{};
        const auto slot = std::strtoul(digits, &end, 10);
        if (*end || slot < firstVirtualSlot) return false;
        out.library = current();
        if (!out.library || slot - firstVirtualSlot >= out.library->size()) return false;
        out.toybox = &(*out.library)[slot - firstVirtualSlot];
        out.file = file;
        return out.toybox->sizes[file] && !isHidden(out.toybox->signature);
    }
    // Hot path for every file the game opens: one length check and one prefix comparison.
    bool virtualPath(const char* path, Virtual& out) {
        if (!path || !saveFolderKnown.load(std::memory_order_acquire) || _strnicmp(path, saveFolder, saveFolderLength)) return false;
        const char* name = path + saveFolderLength;
        if (std::strchr(name, '\\') || std::strchr(name, '/') || !parseName(name, out)) return false;
        // A real file of that name (none is expected) wins.
        return reinterpret_cast<DWORD(WINAPI*)(LPCSTR)>(attributesImport.original)(path) == INVALID_FILE_ATTRIBUTES;
    }

    // Listings of the save folder in progress, by find handle.
    struct Scan {
        HANDLE handle{};
        std::shared_ptr<const Library> library;
        std::vector<std::string> real;  // Names the folder really has.
        std::size_t next{};
        bool appending{};
    };
    std::mutex scanLock;
    std::vector<Scan> scans;

    bool isSaveListing(const char* pattern) {
        constexpr std::string_view tail = "\\savedgame\\win32\\*.*";
        const auto length = pattern ? std::strlen(pattern) : 0;
        return length > tail.size() && length - 3 < sizeof(saveFolder) &&
            !_stricmp(pattern + length - tail.size(), tail.data());
    }
    void fill(WIN32_FIND_DATAA* data, const char* name, std::uint32_t size) {
        std::memset(data, 0, sizeof(*data));
        data->dwFileAttributes = FILE_ATTRIBUTE_ARCHIVE;
        data->nFileSizeLow = size;
        SYSTEMTIME now{};
        GetSystemTime(&now);
        SystemTimeToFileTime(&now, &data->ftLastWriteTime);
        data->ftCreationTime = data->ftLastAccessTime = data->ftLastWriteTime;
        strcpy_s(data->cFileName, name);
    }

    HANDLE WINAPI findFirst(LPCSTR pattern, LPWIN32_FIND_DATAA data) {
        const HANDLE handle = reinterpret_cast<HANDLE(WINAPI*)(LPCSTR, LPWIN32_FIND_DATAA)>(findFirstImport.original)(pattern, data);
        if (handle == INVALID_HANDLE_VALUE || !data || !isSaveListing(pattern)) return handle;
        if (!saveFolderKnown.load(std::memory_order_acquire)) {
            saveFolderLength = std::strlen(pattern) - 3;  // Keeps the trailing backslash.
            std::memcpy(saveFolder, pattern, saveFolderLength);
            saveFolder[saveFolderLength] = '\0';
            saveFolderKnown.store(true, std::memory_order_release);
        }
        // The first listing waits for the library and, briefly, the servers; later ones never block.
        static std::atomic<bool> waited{};
        if (!waited.exchange(true)) {
            if (WaitForSingleObject(ready, 60000) != WAIT_OBJECT_0) log("Toy Box Share: library not ready; listing without it.");
            if (!servers.empty() && WaitForSingleObject(serversDone, serverWait) != WAIT_OBJECT_0)
                log("Toy Box Share: servers still downloading; their new Toy Boxes appear after a save or at the next start.");
        }
        Scan scan;
        scan.handle = handle;
        scan.library = current();
        if (!scan.library || scan.library->empty()) return handle;
        scan.real.emplace_back(data->cFileName);
        std::lock_guard guard(scanLock);
        scans.push_back(std::move(scan));
        return handle;
    }
    BOOL WINAPI findNext(HANDLE handle, LPWIN32_FIND_DATAA data) {
        const auto original = reinterpret_cast<BOOL(WINAPI*)(HANDLE, LPWIN32_FIND_DATAA)>(findNextImport.original);
        std::unique_lock guard(scanLock);
        Scan* scan{};
        for (auto& item : scans) if (item.handle == handle) scan = &item;
        if (!scan) {
            guard.unlock();
            return original(handle, data);
        }
        if (!scan->appending) {
            if (original(handle, data)) {
                scan->real.emplace_back(data->cFileName);
                return TRUE;
            }
            if (GetLastError() != ERROR_NO_MORE_FILES) return FALSE;
            scan->appending = true;
        }
        const auto& list = *scan->library;
        while (scan->next < list.size() * 3) {
            const auto index = scan->next / 3;
            const int file = static_cast<int>(scan->next % 3);
            ++scan->next;
            const auto& toybox = list[index];
            if (!toybox.sizes[file] || isHidden(toybox.signature)) continue;
            char name[32];
            sprintf_s(name, "%s%u", prefixes[file], static_cast<unsigned>(firstVirtualSlot + index));
            bool exists = false;
            for (const auto& real : scan->real) exists = exists || !_stricmp(real.c_str(), name);
            if (exists) continue;
            fill(data, name, toybox.sizes[file]);
            return TRUE;
        }
        SetLastError(ERROR_NO_MORE_FILES);
        return FALSE;
    }
    BOOL WINAPI findClose(HANDLE handle) {
        {
            std::lock_guard guard(scanLock);
            for (auto item = scans.begin(); item != scans.end(); ++item)
                if (item->handle == handle) { scans.erase(item); break; }
        }
        return reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(findCloseImport.original)(handle);
    }
    HANDLE WINAPI createFile(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile) {
        Virtual target;
        if (virtualPath(name, target)) {
            if ((access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) ||
                (disposition != OPEN_EXISTING && disposition != OPEN_ALWAYS)) {
                SetLastError(ERROR_ACCESS_DENIED);  // Catalogue Toy Boxes are read-only.
                return INVALID_HANDLE_VALUE;
            }
            return CreateFileW(target.toybox->files[target.file].c_str(), GENERIC_READ, FILE_SHARE_READ, security,
                OPEN_EXISTING, flags & ~FILE_FLAG_DELETE_ON_CLOSE, nullptr);
        }
        return reinterpret_cast<HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE)>(
            createFileImport.original)(name, access, share, security, disposition, flags, templateFile);
    }
    DWORD WINAPI attributes(LPCSTR name) {
        Virtual target;
        if (virtualPath(name, target)) return GetFileAttributesW(target.toybox->files[target.file].c_str());
        return reinterpret_cast<DWORD(WINAPI*)(LPCSTR)>(attributesImport.original)(name);
    }
    BOOL WINAPI attributesEx(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
        Virtual target;
        if (virtualPath(name, target)) return GetFileAttributesExW(target.toybox->files[target.file].c_str(), level, data);
        return reinterpret_cast<BOOL(WINAPI*)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID)>(attributesExImport.original)(name, level, data);
    }
    BOOL WINAPI deleteFile(LPCSTR name) {
        Virtual target;
        if (virtualPath(name, target)) {
            // Deleting from the menu hides the Toy Box until the next start; its package stays.
            AcquireSRWLockExclusive(&libraryLock);
            hidden.push_back(target.toybox->signature);
            ReleaseSRWLockExclusive(&libraryLock);
            log("Toy Box Share: %s hidden for this session (remove its package to delete it).", target.toybox->name.c_str());
            return TRUE;
        }
        return reinterpret_cast<BOOL(WINAPI*)(LPCSTR)>(deleteImport.original)(name);
    }
}

bool toyBoxShareActive() noexcept { return active; }

bool installToyBoxShare(HMODULE host, const Config& options) {
    if (!options.toyBoxShare) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!codeMatches(base + slotLimitRva, slotLimitCode)) {
        log("Toy Box Share: slot count code differs; nothing changed.");
        return false;
    }
    gameFolder = pluginDirectory();
    cacheFolder = gameFolder + L"toyboxes\\.cache";
    CreateDirectoryW((gameFolder + L"toyboxes").c_str(), nullptr);
    CreateDirectoryW(cacheFolder.c_str(), nullptr);
    SetFileAttributesW(cacheFolder.c_str(), FILE_ATTRIBUTE_HIDDEN);
    for (std::size_t start = 0; start < options.toyBoxServers.size();) {
        auto end = options.toyBoxServers.find_first_of(L" ;,\t", start);
        if (end == std::wstring::npos) end = options.toyBoxServers.size();
        if (end > start) servers.push_back(options.toyBoxServers.substr(start, end - start));
        start = end + 1;
    }
    maximum = options.toyBoxMaximum;
    serverLimits.maxPackage = static_cast<std::uint64_t>(options.toyBoxServerPackageMB) << 20;
    serverLimits.maxMirror = static_cast<std::uint64_t>(options.toyBoxServerMirrorMB) << 20;
    ready = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    serversDone = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!ready || !serversDone) return false;
    struct Import { ImportPatch* patch; const char* name; void* detour; };
    const Import imports[] = {
        {&findFirstImport, "FindFirstFileA", reinterpret_cast<void*>(&findFirst)},
        {&findNextImport, "FindNextFileA", reinterpret_cast<void*>(&findNext)},
        {&findCloseImport, "FindClose", reinterpret_cast<void*>(&findClose)},
        {&createFileImport, "CreateFileA", reinterpret_cast<void*>(&createFile)},
        {&attributesImport, "GetFileAttributesA", reinterpret_cast<void*>(&attributes)},
        {&attributesExImport, "GetFileAttributesExA", reinterpret_cast<void*>(&attributesEx)},
        {&deleteImport, "DeleteFileA", reinterpret_cast<void*>(&deleteFile)},
    };
    for (const auto& item : imports)
        if (!item.patch->install(host, "KERNEL32.dll", item.name, item.detour)) {
            for (const auto& undo : imports) undo.patch->remove();
            log("Toy Box Share: the game's %s import was not found; nothing changed.", item.name);
            return false;
        }
    slotLimit = reinterpret_cast<std::uint32_t*>(base + slotLimitRva + 1);
    const HANDLE thread = CreateThread(nullptr, 0, &worker, nullptr, 0, nullptr);
    if (!thread) {
        SetEvent(ready);
        SetEvent(serversDone);
        log("Toy Box Share: background thread refused (Win32 %lu); no Toy Boxes listed.", GetLastError());
    } else {
        SetThreadPriority(thread, THREAD_PRIORITY_BELOW_NORMAL);
        CloseHandle(thread);
    }
    active = true;
    char folder[MAX_PATH * 3]{};
    WideCharToMultiByte(CP_UTF8, 0, gameFolder.c_str(), -1, folder, sizeof(folder), nullptr, nullptr);
    log("Toy Box Share: enabled; Toy Boxes from %stoyboxes%s.", folder,
        servers.empty() ? "" : " and the configured servers");
    return true;
}
}
