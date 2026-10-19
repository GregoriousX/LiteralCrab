// SPDX-License-Identifier: GPL-3.0-only
// Integrates the two startup call sites documented by DI1-PS3-Dubbing's
// language_startup.cpp. The INI selects a language; packs retain ownership of
// locale definitions, dictionaries, fonts and voices; the mod asset loader exposes
// added native audio files before startup.
#include "di1/language_switcher.hpp"
#include "di1/language_policy.hpp"
#include <array>
#include <cstring>
#include <cwchar>

namespace crabe::di1 {
namespace {
constexpr unsigned initialRva = 0x61F275, steamRva = 0xADC5C2, selectRva = 0x27B2E0, supportedRva = 0x26F080;
constexpr unsigned localeRva = 0x127E43C;
using Select = unsigned(__thiscall*)(void*, const char*);
using Supported = bool(__thiscall*)(void*, const char*);
Select original{};
Supported supported{};
LanguageChoice selected;
std::uintptr_t installedBase{};
std::array<std::int32_t, 2> savedCalls{};

unsigned selectLanguage(void* locale, const char* incoming, const char* stage) {
    if (supported(locale, selected.name.c_str())) {
        const auto result = original(locale, selected.name.c_str());
        if (result) {
            log("LanguageSwitcher: %s selected %s (native result=%u).", stage, selected.name.c_str(), result);
            return result;
        }
        log("LanguageSwitcher: %s could not select %s; retaining native language.", stage, selected.name.c_str());
    } else log("LanguageSwitcher: %s %s is unsupported by the loaded locale; retaining native language.", stage, selected.name.c_str());
    return original(locale, incoming);
}
unsigned __fastcall early(void* locale, void*, const char* incoming) { return selectLanguage(locale, incoming, "initial setup"); }
unsigned __fastcall steam(void* locale, void*, const char* incoming) { return selectLanguage(locale, incoming, "Steam callback"); }

bool dictionariesPresent(HMODULE host, AssetProvider assets, const LanguageChoice& choice) {
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(host, path, static_cast<DWORD>(std::size(path)));
    const auto* slash = length && length < std::size(path) ? wcsrchr(path, L'\\') : nullptr;
    const std::wstring game = slash ? std::wstring(path, static_cast<std::size_t>(slash + 1 - path)) : L"";
    for (const auto* part : {"main", "ca", "in", "lr", "mu", "pi", "tb", "ts"}) {
        const std::string name = "lang/" + std::string(part) + "/" + choice.dictionary + "_win32.dct";
        std::string data;
        if (assets && assets(name.c_str(), data) && !data.empty()) continue;
        // Also accept deliberately installed loose language files. Files under
        // disabled mod folders are not found here and never enable the override.
        std::wstring relative(name.begin(), name.end());
        for (auto& ch : relative) if (ch == L'/') ch = L'\\';
        WIN32_FILE_ATTRIBUTE_DATA info{};
        if (!game.empty() && GetFileAttributesExW((game + L"assets\\" + relative).c_str(), GetFileExInfoStandard, &info) &&
            !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && (info.nFileSizeHigh || info.nFileSizeLow)) continue;
        log("LanguageSwitcher: %s unavailable: missing enabled language asset %s; native language retained.", choice.name.c_str(), name.c_str());
        return false;
    }
    return true;
}
}

bool installLanguageSwitcher(HMODULE host, const Config& options, AssetProvider assets) {
    const auto choice = languageChoice(options.languageSwitcher);
    if (!choice.valid) { log("LanguageSwitcher: invalid locale name; native startup untouched."); return false; }
    if (!choice.enabled) return true;
    if (!host || installedBase) return false;
    if (!choice.factory && !dictionariesPresent(host, options.mods ? assets : nullptr, choice)) return false;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!host || !codeMatches(base + initialRva - 6, "50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 8D 4C 24 04") ||
        !callsTo(base, initialRva, selectRva) ||
        !codeMatches(base + 0xADC5B0, "E8 ?? ?? ?? ?? 8B C8 E8 ?? ?? ?? ?? 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? C3") ||
        !callsTo(base, 0xADC5B0, 0x92160) || !callsTo(base, 0xADC5B7, 0x92020) || !callsTo(base, steamRva, selectRva) ||
        *reinterpret_cast<const std::uintptr_t*>(base + initialRva - 4) != base + localeRva ||
        *reinterpret_cast<const std::uintptr_t*>(base + 0xADC5BE) != base + localeRva ||
        !codeMatches(base + selectRva, "56 8B 74 24 08 57 56 83 C1 08 E8") || !callsTo(base, selectRva + 10, 0x26EC40) ||
        !codeMatches(base + 0x27B309, "33 C0 85 FF 5F 0F 95 C0 5E C2 04 00") ||
        !codeMatches(base + supportedRva, "53 56 57 8B F9 8B 47 24 33 F6 39 70 10 7E 23")) {
        log("LanguageSwitcher: locale startup profile differs or is already patched; no calls changed. Remove PS3Languages.asi when using this option.");
        return false;
    }
    constexpr unsigned sites[] = {initialRva, steamRva};
    selected = choice; // Allocate before changing either page's protection.
    DWORD protections[2]{};
    if (!VirtualProtect(reinterpret_cast<void*>(base + sites[0]), 5, PAGE_EXECUTE_READWRITE, &protections[0])) return false;
    if (!VirtualProtect(reinterpret_cast<void*>(base + sites[1]), 5, PAGE_EXECUTE_READWRITE, &protections[1])) {
        DWORD ignored{}; VirtualProtect(reinterpret_cast<void*>(base + sites[0]), 5, protections[0], &ignored); return false;
    }
    original = reinterpret_cast<Select>(base + selectRva);
    supported = reinterpret_cast<Supported>(base + supportedRva);
    const std::uintptr_t wrappers[] = {reinterpret_cast<std::uintptr_t>(&early), reinterpret_cast<std::uintptr_t>(&steam)};
    for (unsigned i = 0; i < 2; ++i) {
        auto* operand = reinterpret_cast<std::int32_t*>(base + sites[i] + 1);
        savedCalls[i] = *operand;
        *operand = static_cast<std::int32_t>(wrappers[i] - (base + sites[i] + 5));
        DWORD ignored{}; VirtualProtect(reinterpret_cast<void*>(base + sites[i]), 5, protections[i], &ignored);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(base + sites[i]), 5);
    }
    installedBase = base;
    log("LanguageSwitcher: %s configured before audio startup and retained through Steam; registry untouched.", selected.name.c_str());
    return true;
}
void removeLanguageSwitcher() noexcept {
    if (!installedBase) return;
    constexpr unsigned sites[] = {initialRva, steamRva};
    const std::uintptr_t wrappers[] = {reinterpret_cast<std::uintptr_t>(&early), reinterpret_cast<std::uintptr_t>(&steam)};
    for (unsigned i = 0; i < 2; ++i) {
        auto* operand = reinterpret_cast<std::int32_t*>(installedBase + sites[i] + 1);
        if (installedBase + sites[i] + 5 + *operand != wrappers[i]) continue;
        DWORD old{};
        if (!VirtualProtect(operand, 4, PAGE_EXECUTE_READWRITE, &old)) continue;
        *operand = savedCalls[i];
        DWORD ignored{}; VirtualProtect(operand, 4, old, &ignored);
        FlushInstructionCache(GetCurrentProcess(), operand, 4);
    }
    installedBase = 0;
}
}
