// SPDX-License-Identifier: GPL-3.0-only
// Exercise the real INI parser, observer and in-process capture without a game.
// Only module-path discovery is redirected to this test's own fixture directory.
#include "di1/common.hpp"
#include <filesystem>
#include <fstream>
#include <iterator>
#include <cstdio>
#include <cwchar>
#include <cstring>
#include <string>
#include <system_error>

namespace {
std::wstring fixtureModule;
DWORD WINAPI fixtureModuleFileName(HMODULE, wchar_t* path, DWORD capacity) {
    if (fixtureModule.size() >= capacity) return capacity;
    std::wcscpy(path, fixtureModule.c_str());
    return static_cast<DWORD>(fixtureModule.size());
}
}
#define GetModuleFileNameW fixtureModuleFileName
#include "../../src/di1/common.cpp"
#undef GetModuleFileNameW
// Independent production translation units share an anonymous namespace only
// in this probe; their directory parameters legitimately shadow common's state.
#pragma warning(push)
#pragma warning(disable: 4459)
#include "../../src/di1/diagnostics.cpp"
#include "../../src/di1/auto_capture.cpp"
#pragma warning(pop)

namespace crabe::di1 {
namespace { void (*graphicsCallback)(){}; }
void setGraphicsStartupObserver(void (*callback)()) { graphicsCallback = callback; }
}
namespace {
unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
LONG WINAPI nativeFilter(EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
bool handledException() {
    __try { RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
    return false;
}
std::string contents(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), {}};
}
void reset(bool resetConfig = true) {
    using namespace crabe::di1;
    removeExceptionDiagnostics();
    CHECK(stopAutomaticCapture());
    graphicsCallback = nullptr;
    if (logFile != INVALID_HANDLE_VALUE) { CloseHandle(logFile); logFile = INVALID_HANDLE_VALUE; }
    if (resetConfig) settings = Config{};
    directory.clear(); iniPath[0] = L'\0';
}
void load(const std::filesystem::path& folder, const std::string* ini) {
    namespace fs = std::filesystem;
    fs::create_directories(folder);
    if (ini) { std::ofstream file(folder / L"LiteralCrab.ini", std::ios::binary); file << *ini; }
    fixtureModule = (folder / L"LiteralCrab.asi").wstring();
    crabe::di1::openLogAndConfig(nullptr);
}
void disabled(const std::filesystem::path& folder) {
    using namespace crabe::di1;
    CHECK(!config().diagnostics && !config().exceptionDiagnostics && !config().crashReports &&
          !config().traceAudioBanks && !config().modTrace && !config().traceCharacterFreedom);
    const auto previous = SetUnhandledExceptionFilter(nativeFilter);
    installDiagnosticStartup(GetModuleHandleW(nullptr), GetModuleHandleW(nullptr), pluginDirectory(), config());
    CHECK(!observer && exceptionFile == INVALID_HANDLE_VALUE);
    CHECK(!armed && !started && !writerThread && !dumpLibrary && !graphicsCallback);
    SetLastError(0x1234ABCD); startAutomaticCapture();
    CHECK(GetLastError() == 0x1234ABCD && !writerThread && !dumpLibrary);
    CHECK(SetUnhandledExceptionFilter(previous) == nativeFilter);
    CHECK(!std::filesystem::exists(folder / L"LiteralCrab-exceptions.log"));
    CHECK(!std::filesystem::exists(folder / L"LiteralCrab-diagnostics"));
    CHECK(contents(folder / L"LiteralCrab.log").find("Diagnostics: build=") == std::string::npos);
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace crabe::di1;
    namespace fs = std::filesystem;
    wchar_t temp[32768]{};
    CHECK(GetTempPathW(_countof(temp), temp) != 0);
    const auto root = fs::path(temp) / (L"di1-config-startup-" + std::to_wstring(GetCurrentProcessId()));
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    CHECK(!cleanupError);
    const Config defaults;
    CHECK(!defaults.toolsInAnyPlayset);
    CHECK(!defaults.exceptionDiagnostics && !defaults.crashReports && !defaults.traceAudioBanks);

    load(root / L"missing", nullptr);
    disabled(root / L"missing");
    reset();

    // A legacy gameplay INI lacks all new diagnostic keys. Functional options
    // remain enabled, while none of the diagnostic observers/worker is armed.
    const std::string oldIni = "[Gameplay]\nCharactersInAnyPlayset=1\n[Mods]\nEnabled=1\n";
    load(root / L"legacy", &oldIni);
    CHECK(config().charactersInAnyPlayset && config().mods);
    CHECK(!config().toolsInAnyPlayset);
    disabled(root / L"legacy");
    reset();

    CHECK(argc == 2);
    if (argc == 2) {
        const auto shipped = contents(argv[1]);
        CHECK(!shipped.empty());
        load(root / L"shipped", &shipped);
        CHECK(config().testExperienceMultiplier == 1 && config().toyBoxLimitScale == 1);
        CHECK(!config().toolsInAnyPlayset);
        CHECK(!config().toyStoreInfinity && !config().toyStoreInfinityAll);
        disabled(root / L"shipped");
        reset();
    }

    for (const bool characters : {false, true}) for (const bool tools : {false, true}) {
        const std::string ini = "[Gameplay]\nCharactersInAnyPlayset=" + std::to_string(characters) +
            "\nToolsInAnyPlayset=" + std::to_string(tools) + "\n[Mods]\nEnabled=1\n";
        load(root / (L"tools-" + std::to_wstring(characters) + L"-" + std::to_wstring(tools)), &ini);
        CHECK(config().charactersInAnyPlayset == characters);
        CHECK(config().toolsInAnyPlayset == (characters && tools));
        reset();
    }
    for (const bool infinity : {false, true}) for (const bool all : {false, true}) {
        const std::string ini = "[ToyStore]\nInfinity=" + std::to_string(infinity) + "\nInfinityAll=" + std::to_string(all) + "\n";
        load(root / (L"store-" + std::to_wstring(infinity) + L"-" + std::to_wstring(all)), &ini);
        CHECK(config().toyStoreInfinity == infinity);
        CHECK(config().toyStoreInfinityAll == (infinity && all));
        reset();
    }
    load(root / L"legacy-after-tools", &oldIni);
    CHECK(!config().toyStoreInfinity && !config().toyStoreInfinityAll);
    CHECK(!config().toolsInAnyPlayset);
    reset();

    const std::string exceptions = "[Diagnostics]\nExceptionLog=1\n";
    load(root / L"exceptions", &exceptions);
    CHECK(config().exceptionDiagnostics && !config().crashReports && !config().traceAudioBanks);
    installDiagnosticStartup(GetModuleHandleW(nullptr), GetModuleHandleW(nullptr), pluginDirectory(), config());
    CHECK(observer && !armed && !writerThread && !graphicsCallback);
    CHECK(handledException() && eventCount == 1);
    reset();

    const std::string crashOnly = "[Diagnostics]\nCrashReports=1\n";
    load(root / L"crash", &crashOnly);
    CHECK(!config().exceptionDiagnostics && config().crashReports);
    const auto previous = SetUnhandledExceptionFilter(nativeFilter);
    installDiagnosticStartup(GetModuleHandleW(nullptr), GetModuleHandleW(nullptr), pluginDirectory(), config());
    CHECK(!observer && armed && !writerThread && !dumpLibrary && graphicsCallback == &startAutomaticCapture);
    CHECK(SetUnhandledExceptionFilter(nativeFilter) == nativeFilter); // Deferred startup has not replaced it.
    if (graphicsCallback) graphicsCallback();
    CHECK(writerThread && dumpLibrary && !observer && reportNumber == 0);
    CHECK(stopAutomaticCapture());
    CHECK(SetUnhandledExceptionFilter(previous) == nativeFilter);
    reset();

    const std::string qa = "[Diagnostics]\nCrashReports=1\nExceptionLog=1\nTraceAudioBanks=1\n"
                           "[Display]\nDiagnostics=1\n[Mods]\nEnabled=1\nTrace=1\n"
                           "[Gameplay]\nCharactersInAnyPlayset=1\nTraceCharacterFreedom=1\n";
    load(root / L"qa", &qa);
    CHECK(config().diagnostics && config().crashReports && config().exceptionDiagnostics &&
          config().traceAudioBanks && config().modTrace && config().traceCharacterFreedom);
    CHECK(config().mods && config().charactersInAnyPlayset && config().testExperienceMultiplier == 1);
    installDiagnosticStartup(GetModuleHandleW(nullptr), GetModuleHandleW(nullptr), pluginDirectory(), config());
    CHECK(observer && armed && !writerThread && graphicsCallback);
    if (graphicsCallback) graphicsCallback();
    CHECK(writerThread && observer && reportNumber == 0);
    reset(false);

    // Reading another legacy configuration after QA must restore missing keys to
    // their release defaults rather than inheriting the previous diagnostic state.
    load(root / L"legacy-after-qa", &oldIni);
    CHECK(config().charactersInAnyPlayset && config().mods);
    disabled(root / L"legacy-after-qa");
    reset();
    fs::remove_all(root, cleanupError);
    CHECK(!cleanupError);
    std::printf("Missing/legacy/shipped INI defaults and explicit QA observer/deferred-worker startup: %s\n",
        failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
