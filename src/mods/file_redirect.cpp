// SPDX-License-Identifier: GPL-3.0-only
#include "mods/file_redirect.hpp"
#include "minhook/MinHook.h"
#include <windows.h>
#include <cstring>
#include <cwchar>

namespace crabe::mods {
namespace {
    AssetIndex index;
    LogFunction log = [](const char*, ...) {};
    bool trace{};

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

    template <class Char> const std::wstring* lookup(const Char* fileName) noexcept {
        if (!fileName || index.empty()) return nullptr;
        const auto length = std::char_traits<Char>::length(fileName);
        char path[maxAssetPath];
        const auto size = assetPath(std::basic_string_view<Char>(fileName, length), path);
        const auto* target = size ? index.find(std::string_view(path, size)) : nullptr;
        if (target && trace) log("Asset %.*s served from %ls.", static_cast<int>(size), path, target->c_str());
        return target;
    }
    // Writes, deletes and creations always reach the original path.
    bool readOnly(DWORD access, DWORD disposition) noexcept {
        return !(access & (GENERIC_WRITE | GENERIC_ALL | FILE_WRITE_DATA | FILE_APPEND_DATA | DELETE)) &&
            (disposition == OPEN_EXISTING || disposition == OPEN_ALWAYS);
    }

    HANDLE WINAPI hookCreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile) {
        if (readOnly(access, disposition))
            if (const auto* target = lookup(name))
                return createFileW(target->c_str(), access, share, security, OPEN_EXISTING, flags, templateFile);
        return createFileA(name, access, share, security, disposition, flags, templateFile);
    }
    HANDLE WINAPI hookCreateFileW(LPCWSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES security,
        DWORD disposition, DWORD flags, HANDLE templateFile) {
        if (readOnly(access, disposition))
            if (const auto* target = lookup(name))
                return createFileW(target->c_str(), access, share, security, OPEN_EXISTING, flags, templateFile);
        return createFileW(name, access, share, security, disposition, flags, templateFile);
    }
    DWORD WINAPI hookAttributesA(LPCSTR name) {
        if (const auto* target = lookup(name)) return attributesW(target->c_str());
        return attributesA(name);
    }
    DWORD WINAPI hookAttributesW(LPCWSTR name) {
        if (const auto* target = lookup(name)) return attributesW(target->c_str());
        return attributesW(name);
    }
    BOOL WINAPI hookAttributesExA(LPCSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
        if (const auto* target = lookup(name)) return attributesExW(target->c_str(), level, data);
        return attributesExA(name, level, data);
    }
    BOOL WINAPI hookAttributesExW(LPCWSTR name, GET_FILEEX_INFO_LEVELS level, LPVOID data) {
        if (const auto* target = lookup(name)) return attributesExW(target->c_str(), level, data);
        return attributesExW(name, level, data);
    }
}

const std::wstring* redirectedAsset(const char* fileName) noexcept { return lookup(fileName); }
const std::wstring* redirectedAsset(const wchar_t* fileName) noexcept { return lookup(fileName); }
void setAssetIndexForTest(AssetIndex loaded) { index = std::move(loaded); }

bool installFileRedirect(AssetIndex loaded, LogFunction logger, bool traceFiles) {
    index = std::move(loaded);
    log = logger;
    trace = traceFiles;
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
    bool ok = kernel32 != nullptr;
    for (std::size_t item = 0; ok && item < std::size(hooks); ++item) {
        targets[item] = reinterpret_cast<void*>(GetProcAddress(kernel32, hooks[item].name));
        ok = targets[item] && MH_CreateHook(targets[item], hooks[item].detour, hooks[item].original) == MH_OK;
        if (!ok) log("Mods: cannot hook kernel32!%s.", hooks[item].name);
    }
    // Enabled one by one: MH_ApplyQueued would also apply hooks other modules have queued.
    for (std::size_t item = 0; ok && item < std::size(hooks); ++item) {
        ok = MH_EnableHook(targets[item]) == MH_OK;
        if (!ok) log("Mods: cannot enable the kernel32!%s hook.", hooks[item].name);
    }
    if (!ok) {
        for (const auto target : targets) if (target) { MH_DisableHook(target); MH_RemoveHook(target); }
        index = {};
    }
    return ok;
}
}
