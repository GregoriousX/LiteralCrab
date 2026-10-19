// SPDX-License-Identifier: GPL-3.0-only
#include "mods/file_redirect.hpp"
#include "minhook/MinHook.h"
#include <windows.h>
#include <cstring>
#include <cwchar>
#include <atomic>

namespace crabe::mods {
namespace {
    AssetIndex index;
    LogFunction log = [](const char*, ...) {};
    bool trace{};
    std::atomic<unsigned> audioOpens{};

    using CreateFileAFn = HANDLE(WINAPI*)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    using CreateFileWFn = HANDLE(WINAPI*)(LPCWSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
    using AttributesAFn = DWORD(WINAPI*)(LPCSTR);
    using AttributesWFn = DWORD(WINAPI*)(LPCWSTR);
    using AttributesExAFn = BOOL(WINAPI*)(LPCSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
    using AttributesExWFn = BOOL(WINAPI*)(LPCWSTR, GET_FILEEX_INFO_LEVELS, LPVOID);
    CreateFileAFn createFileA{};
    CreateFileWFn createFileW{};
    AttributesAFn attributesA{};
    AttributesWFn attributesW{};
    AttributesExAFn attributesExA{};
    AttributesExWFn attributesExW{};

    struct Resolution {
        char path[maxAssetPath]{};
        std::size_t size{};
        const std::wstring* target{};
        bool directory{};
        std::string_view key() const noexcept { return {path, size}; }
    };
    template <class Char> Resolution resolve(const Char* fileName, bool directories = false) noexcept {
        Resolution result;
        if (!fileName) return result;
        auto length = std::char_traits<Char>::length(fileName);
        if (directories) while (length && (fileName[length - 1] == '/' || fileName[length - 1] == '\\')) --length;
        result.size = assetPath(std::basic_string_view<Char>(fileName, length), result.path);
        if (result.size) {
            result.target = index.find(result.key());
            result.directory = directories && !result.target && index.hasDirectory(result.key());
        }
        return result;
    }
    template <class Char> const std::wstring* lookup(const Char* fileName) noexcept {
        return resolve(fileName).target;
    }
    template <class Char> void report(const char* operation, const Char* name, const Resolution& resolved,
        bool success, DWORD error, bool opening = false) {
        if (!resolved.size) return;
        const bool dialogue = resolved.key().starts_with("sound/dialog/");
        // Native requests reveal the selected audio locale even when startup
        // language forcing is off or the player changes language in the menu.
        const bool audio = dialogue && opening && resolved.key().substr(13).find('/') != std::string_view::npos;
        if (!trace && !(resolved.target && !success) && !(audio && (!success || audioOpens.fetch_add(1) < 2048))) return;
        const auto destination = resolved.target ? resolved.target->c_str() : resolved.directory ? L"<mod directory>" : L"<native path>";
        if constexpr (sizeof(Char) == sizeof(char))
            log("Mods: %s requested=%s resolved=%ls result=%s error=%lu.", operation, name, destination, success ? "ok" : "failed", success ? 0ul : error);
        else
            log("Mods: %s requested=%ls resolved=%ls result=%s error=%lu.", operation, name, destination, success ? "ok" : "failed", success ? 0ul : error);
        if (audio) {
            const auto folder = resolved.key().substr(13);
            const auto end = folder.find('/');
            log("Mods: native audio language=%.*s bank=%.*s.", static_cast<int>(end == std::string_view::npos ? folder.size() : end), folder.data(),
                static_cast<int>(resolved.size), resolved.path);
        }
        SetLastError(error);  // Logging must not change the caller's Win32 result.
    }
    // Writes, deletes and creations always reach the original path.
    bool readOnly(DWORD access, DWORD disposition) noexcept {
        return !(access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA |
            FILE_WRITE_ATTRIBUTES | WRITE_DAC | WRITE_OWNER | DELETE)) &&
            (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS);
    }

    HANDLE WINAPI hookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile) {
        if (!readOnly(access, disposition)) return createFileA(name, access, share, security, disposition, flags, templateFile);
        const auto resolved = resolve(name);
        const auto handle = resolved.target ? createFileW(resolved.target->c_str(), access, share, security, OPEN_EXISTING, flags, templateFile)
            : createFileA(name, access, share, security, disposition, flags, templateFile);
        if (resolved.target && disposition == OPEN_ALWAYS && handle != INVALID_HANDLE_VALUE) SetLastError(ERROR_ALREADY_EXISTS);
        const auto error = GetLastError();
        report("CreateFileA", name, resolved, handle != INVALID_HANDLE_VALUE, error, true);
        return handle;
    }
    HANDLE WINAPI hookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile) {
        if (!readOnly(access, disposition)) return createFileW(name, access, share, security, disposition, flags, templateFile);
        const auto resolved = resolve(name);
        const auto handle = createFileW(resolved.target ? resolved.target->c_str() : name, access, share, security,
            resolved.target ? OPEN_EXISTING : disposition, flags, templateFile);
        if (resolved.target && disposition == OPEN_ALWAYS && handle != INVALID_HANDLE_VALUE) SetLastError(ERROR_ALREADY_EXISTS);
        const auto error = GetLastError();
        report("CreateFileW", name, resolved, handle != INVALID_HANDLE_VALUE, error, true);
        return handle;
    }
    DWORD WINAPI hookAttributesA(LPCSTR name) {
        const auto resolved = resolve(name, true);
        const auto incoming = GetLastError();
        auto result = resolved.target ? attributesW(resolved.target->c_str()) : attributesA(name);
        if (result == INVALID_FILE_ATTRIBUTES && resolved.directory) { result = FILE_ATTRIBUTE_DIRECTORY; SetLastError(incoming); }
        const auto error = GetLastError();
        report("GetFileAttributesA", name, resolved, result != INVALID_FILE_ATTRIBUTES, error);
        return result;
    }
    DWORD WINAPI hookAttributesW(LPCWSTR name) {
        const auto resolved = resolve(name, true);
        const auto incoming = GetLastError();
        auto result = attributesW(resolved.target ? resolved.target->c_str() : name);
        if (result == INVALID_FILE_ATTRIBUTES && resolved.directory) { result = FILE_ATTRIBUTE_DIRECTORY; SetLastError(incoming); }
        const auto error = GetLastError();
        report("GetFileAttributesW", name, resolved, result != INVALID_FILE_ATTRIBUTES, error);
        return result;
    }
    BOOL WINAPI hookAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
        const auto resolved = resolve(name, true);
        const auto incoming = GetLastError();
        auto result = resolved.target ? attributesExW(resolved.target->c_str(), level, data) : attributesExA(name, level, data);
        if (!result && resolved.directory && level == GetFileExInfoStandard && data) {
            *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(data) = {};
            static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(data)->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            result = TRUE; SetLastError(incoming);
        }
        const auto error = GetLastError();
        report("GetFileAttributesExA", name, resolved, result != FALSE, error);
        return result;
    }
    BOOL WINAPI hookAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
        const auto resolved = resolve(name, true);
        const auto incoming = GetLastError();
        auto result = attributesExW(resolved.target ? resolved.target->c_str() : name, level, data);
        if (!result && resolved.directory && level == GetFileExInfoStandard && data) {
            *static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(data) = {};
            static_cast<WIN32_FILE_ATTRIBUTE_DATA*>(data)->dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
            result = TRUE; SetLastError(incoming);
        }
        const auto error = GetLastError();
        report("GetFileAttributesExW", name, resolved, result != FALSE, error);
        return result;
    }
}

const std::wstring* redirectedAsset(const char* fileName) noexcept { return lookup(fileName); }
bool readAssetMember(const char* name, std::string& data) {
    char key[maxAssetPath];
    const auto length = name ? requestName(name, key) : 0;
    const auto* member = length ? index.member(std::string_view(key, length)) : nullptr;
    return member && index.read(*member, data);
}
const std::wstring* redirectedAsset(const wchar_t* fileName) noexcept { return lookup(fileName); }
void setAssetIndexForTest(AssetIndex loaded) { index = std::move(loaded); }

bool installFileRedirect(AssetIndex loaded, LogFunction logger, bool traceFiles) {
    index = std::move(loaded);
    log = logger;
    trace = traceFiles;
    audioOpens = 0;
    if (index.redirects.empty()) return true;
    const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
    struct Hook {
        const char* name;
        void* detour;
        void** original;
    };
    const Hook hooks[] = {
        {"CreateFileA", reinterpret_cast<void*>(&hookCreateFileA), reinterpret_cast<void**>(&createFileA)},
        {"CreateFileW", reinterpret_cast<void*>(&hookCreateFileW), reinterpret_cast<void**>(&createFileW)},
        {"GetFileAttributesA", reinterpret_cast<void*>(&hookAttributesA), reinterpret_cast<void**>(&attributesA)},
        {"GetFileAttributesW", reinterpret_cast<void*>(&hookAttributesW), reinterpret_cast<void**>(&attributesW)},
        {"GetFileAttributesExA", reinterpret_cast<void*>(&hookAttributesExA), reinterpret_cast<void**>(&attributesExA)},
        {"GetFileAttributesExW", reinterpret_cast<void*>(&hookAttributesExW), reinterpret_cast<void**>(&attributesExW)},
    };
    void* targets[std::size(hooks)]{};
    std::size_t created{};
    bool ok = kernel32 != nullptr;
    for (std::size_t item = 0; ok && item < std::size(hooks); ++item) {
        targets[item] = reinterpret_cast<void*>(GetProcAddress(kernel32, hooks[item].name));
        ok = targets[item] && MH_CreateHook(targets[item], hooks[item].detour, hooks[item].original) == MH_OK;
        if (ok) ++created;
        if (!ok) log("Mods: cannot hook kernel32!%s.", hooks[item].name);
    }
    // Enabled one by one: MH_ApplyQueued would also apply hooks other modules have queued.
    for (std::size_t item = 0; ok && item < std::size(hooks); ++item) {
        ok = MH_EnableHook(targets[item]) == MH_OK;
        if (!ok) log("Mods: cannot enable the kernel32!%s hook.", hooks[item].name);
    }
    if (!ok) {
        for (std::size_t item = 0; item < created; ++item) { MH_DisableHook(targets[item]); MH_RemoveHook(targets[item]); }
        index.redirects.clear();  // Members are served without file hooks.
    }
    return ok;
}
}
