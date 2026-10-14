// SPDX-License-Identifier: GPL-3.0-only
#include "di1/language_policy.hpp"
#include "di1/language_switcher.hpp"
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
unsigned assetReads{}, selectionCalls{}, supportCalls{};
bool available = true, accept = true, failForced{}, emptyAsset{};
unsigned missingAsset{};
void* observedLocale{};
const char* observedName{};
std::string supportedName;
std::vector<std::string> assetPaths;
struct ExpectedLanguage { const wchar_t* setting; const char* name; const char* dictionary; bool factory; };
constexpr ExpectedLanguage expectedLanguages[] = {
    {L"EnglishNTSC", "EnglishNTSC", "english_ntsc", true},
    {L"EnglishPAL", "EnglishPAL", "english_pal", false},
    {L"FrenchNTSC", "FrenchNTSC", "frenchcanada", false},
    {L"FrenchPAL", "FrenchPAL", "frenchparisian", true},
    {L"italian", "Italian", "italian", false}, {L"german", "German", "german", true},
    {L"SpanishNTSC", "SpanishNTSC", "spanishlatinamerica", true},
    {L"SpanishPAL", "SpanishPAL", "castilianspanish", false},
    {L"japanese", "Japanese", "japanese", false}, {L"swedish", "Swedish", "swedish", false},
    {L"dutch", "Dutch", "dutch", false}, {L"danish", "Danish", "danish", false},
    {L"norwegian", "Norwegian", "norwegian", false}, {L"polish", "Polish", "polish", false},
    {L"russian", "Russian", "russian", false}, {L"arabic", "Arabic", "arabic", false},
    {L"finnish", "Finnish", "finnish", false},
    {L"BrazilianPortuguese", "BrazilianPortuguese", "brazilianportuguese", true}
};
bool assets(const char* name, std::string& data) {
    ++assetReads;
    if (!name || !std::strstr(name, "lang/") || !std::strstr(name, "_win32.dct")) std::abort();
    assetPaths.emplace_back(name);
    if (!available || assetReads == missingAsset) return false;
    data = emptyAsset ? "" : "synthetic dictionary payload";
    return true;
}
unsigned __fastcall select(void* self, void*, const char* name) {
    ++selectionCalls; observedLocale = self; observedName = name;
    if (failForced && name && !std::strcmp(name, "Polish")) return 0;
    return 0xABCD0001; // Forward the full native unsigned result, not a bool.
}
bool __fastcall supported(void* self, void*, const char* name) {
    ++supportCalls; observedLocale = self; supportedName = name; return accept;
}
bool write(void* destination, const void* source, std::size_t size) {
    DWORD old{};
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(destination, source, size);
    const bool restored = VirtualProtect(destination, size, old, &old) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    return restored;
}
}

int wmain(int argc, wchar_t** argv) {
    using namespace crabe::di1;
    unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
    for (const auto* setting : {L"", L"off", L"OFF", L" auto ", L"default", L"\t"}) {
        const auto choice = languageChoice(setting);
        CHECK(choice.valid && !choice.enabled);
        Config options; options.languageSwitcher = setting;
        CHECK(installLanguageSwitcher(nullptr, options, &assets));
    }
    CHECK(assetReads == 0);
    for (const auto& expected : expectedLanguages) {
        const auto choice = languageChoice(expected.setting);
        CHECK(choice.valid && choice.enabled && choice.name == expected.name);
        CHECK(choice.dictionary == expected.dictionary && choice.factory == expected.factory);
        const auto lower = lowercaseLanguage(expected.name);
        CHECK(languageChoice(std::wstring(lower.begin(), lower.end())).name == expected.name);
    }
    for (const auto& expected : {
        ExpectedLanguage{L"englishus", "EnglishNTSC", "english_ntsc", true},
        {L"englishuk", "EnglishPAL", "english_pal", false},
        {L"frenchcanadian", "FrenchNTSC", "frenchcanada", false},
        {L"frenchcanada", "FrenchNTSC", "frenchcanada", false},
        {L"frenchparisian", "FrenchPAL", "frenchparisian", true},
        {L"spanishlatinamerica", "SpanishNTSC", "spanishlatinamerica", true},
        {L"spanishspain", "SpanishPAL", "castilianspanish", false},
        {L"castilianspanish", "SpanishPAL", "castilianspanish", false}}) {
        const auto choice = languageChoice(expected.setting);
        CHECK(choice.name == expected.name && choice.dictionary == expected.dictionary && choice.factory == expected.factory);
    }
    for (const auto* setting : {L"polish", L"Polish", L"POLISH", L"  polish\t"}) {
        const auto choice = languageChoice(setting);
        CHECK(choice.valid && choice.enabled && !choice.factory && choice.name == "Polish" && choice.dictionary == "polish");
    }
    CHECK(languageChoice(L"english").name == "EnglishNTSC" && languageChoice(L"english").factory);
    CHECK(languageChoice(L"french").name == "FrenchPAL" && languageChoice(L"french").factory);
    CHECK(languageChoice(L"German").factory && languageChoice(L"SpanishNTSC").factory);
    CHECK(languageChoice(L"portuguese").name == "BrazilianPortuguese");
    CHECK(languageChoice(L"russian").name == "Russian" && languageChoice(L"arabic").name == "Arabic");
    CHECK(languageChoice(L"Catalan").name == "Catalan" && languageChoice(L"Catalan").dictionary == "catalan");
    for (const auto* setting : {L"../polish", L"polish/", L"polskią", L"polish.exe", L"polish\n", L"1"})
        CHECK(!languageChoice(setting).valid);
    CHECK(!languageChoice(std::wstring(64, L'a')).valid);

    if (argc >= 2) {
        // Private mapping only: don't execute the game's entry point or change
        // its disk file. Real startup call operands and complete rollback tested.
        HMODULE host = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(host != nullptr);
        if (host) {
            auto* base = reinterpret_cast<unsigned char*>(host);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
            const auto* sections = IMAGE_FIRST_SECTION(nt);
            const IMAGE_SECTION_HEADER* code{};
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
                if (!std::memcmp(sections[i].Name, ".text", 5)) code = &sections[i];
            CHECK(code != nullptr);
            if (code) {
                const std::vector<unsigned char> snapshot(base + code->VirtualAddress, base + code->VirtualAddress + code->Misc.VirtualSize);
                const auto restore = [&](unsigned rva, std::size_t size) {
                    CHECK(write(base + rva, snapshot.data() + rva - code->VirtualAddress, size));
                };
                const auto jump = [&](unsigned rva, std::uintptr_t target) {
                    unsigned char stub[] = {0xE9, 0, 0, 0, 0};
                    const auto relative = static_cast<std::int32_t>(target - reinterpret_cast<std::uintptr_t>(base + rva + sizeof(stub)));
                    std::memcpy(stub + 1, &relative, 4); CHECK(write(base + rva, stub, sizeof(stub)));
                };
                Config options;
                CHECK(installLanguageSwitcher(host, options, &assets));
                CHECK(!std::memcmp(base + code->VirtualAddress, snapshot.data(), snapshot.size()));
                // Invalid/disabled/missing packs leave BOTH original calls intact.
                options.languageSwitcher = L"TestAbsentLocale";
                available = false; assetReads = 0;
                CHECK(!installLanguageSwitcher(host, options, &assets)); CHECK(assetReads == 1);
                available = true; emptyAsset = true; assetReads = 0;
                CHECK(!installLanguageSwitcher(host, options, &assets)); CHECK(assetReads == 1);
                emptyAsset = false;
                for (unsigned i = 1; i <= 8; ++i) {
                    missingAsset = i; assetReads = 0;
                    CHECK(!installLanguageSwitcher(host, options, &assets)); CHECK(assetReads == i);
                }
                missingAsset = 0; options.mods = false; assetReads = 0;
                CHECK(!installLanguageSwitcher(host, options, &assets)); CHECK(assetReads == 0);
                options.mods = true;
                CHECK(!std::memcmp(base + code->VirtualAddress, snapshot.data(), snapshot.size()));
                constexpr unsigned sites[] = {0x61F275, 0xADC5C2};
                std::vector<const wchar_t*> requestedLanguages;
                for (const auto& expected : expectedLanguages) requestedLanguages.push_back(expected.setting);
                for (const auto* alias : {L"english", L"french", L"spanish", L"portuguese", L"englishuk", L"frenchcanadian", L"spanishspain", L"Catalan"})
                    requestedLanguages.push_back(alias);
                for (const auto* requested : requestedLanguages) {
                    options.languageSwitcher = requested; assetReads = 0; assetPaths.clear();
                    CHECK(installLanguageSwitcher(host, options, &assets));
                    const auto choice = languageChoice(requested);
                    CHECK(assetReads == (choice.factory ? 0u : 8u));
                    if (!choice.factory) {
                        const char* parts[] = {"main", "ca", "in", "lr", "mu", "pi", "tb", "ts"};
                        CHECK(assetPaths.size() == 8);
                        for (unsigned i = 0; i < assetPaths.size(); ++i)
                            CHECK(assetPaths[i] == "lang/" + std::string(parts[i]) + "/" + choice.dictionary + "_win32.dct");
                    }
                    CHECK(!installLanguageSwitcher(host, options, &assets)); // One owner only.
                    jump(0x27B2E0, reinterpret_cast<std::uintptr_t>(&select));
                    jump(0x26F080, reinterpret_cast<std::uintptr_t>(&supported));
                    for (const auto site : sites) {
                        // Reproduce native ECX singleton and stack argument at the
                        // exact patched CALL. The CALL operand stays installed.
                        unsigned char prefix[] = {0x8B, 0x44, 0x24, 4, 0x50, 0xB9, 0, 0, 0, 0};
                        const auto singleton = reinterpret_cast<std::uintptr_t>(base + 0x127E43C);
                        std::memcpy(prefix + 6, &singleton, 4);
                        CHECK(write(base + site - 10, prefix, sizeof(prefix)));
                        const unsigned char ret = 0xC3; CHECK(write(base + site + 5, &ret, 1));
                        const auto call = reinterpret_cast<unsigned(__cdecl*)(const char*)>(base + site - 10);
                        static const char nativeName[] = "EnglishNTSC";
                        accept = true; failForced = false; selectionCalls = supportCalls = 0;
                        for (unsigned i = 0; i < 130; ++i) {
                            CHECK(call(nativeName) == 0xABCD0001);
                            CHECK(observedLocale == base + 0x127E43C && observedName && std::strcmp(observedName, choice.name.c_str()) == 0);
                            CHECK(supportedName == choice.name);
                        }
                        CHECK(selectionCalls == 130 && supportCalls == 130);
                        accept = false; selectionCalls = supportCalls = 0;
                        CHECK(call(nativeName) == 0xABCD0001 && observedName == nativeName && selectionCalls == 1 && supportCalls == 1);
                        CHECK(call(nullptr) == 0xABCD0001 && observedName == nullptr);
                        accept = true;
                        if (choice.name == "Polish") {
                            failForced = true; selectionCalls = 0;
                            CHECK(call(nativeName) == 0xABCD0001 && observedName == nativeName && selectionCalls == 2);
                            failForced = false;
                        }
                        restore(site - 10, sizeof(prefix)); restore(site + 5, 1);
                    }
                    restore(0x27B2E0, 5); restore(0x26F080, 5);
                    removeLanguageSwitcher();
                    CHECK(!std::memcmp(base + code->VirtualAddress, snapshot.data(), snapshot.size()));
                }
                // A conflicting later call or relocated operand refuses the whole
                // feature without touching the earlier one or the unrelated patch.
                options.languageSwitcher = L"english";
                for (const auto rva : {0xADC5C3u, 0x61F271u, 0xADC5BEu, 0x26F080u}) {
                    const unsigned char different = base[rva] ^ 1;
                    CHECK(write(base + rva, &different, 1));
                    CHECK(!installLanguageSwitcher(host, options, &assets));
                    CHECK(!std::memcmp(base + sites[0], snapshot.data() + sites[0] - code->VirtualAddress, 5));
                    restore(rva, 1);
                }
                CHECK(!std::memcmp(base + code->VirtualAddress, snapshot.data(), snapshot.size()));
            }
            FreeLibrary(host);
        }
    }
    std::printf("Language switcher policy%s: %s\n", argc >= 2 ? "/native ABI/rollback" : "", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
