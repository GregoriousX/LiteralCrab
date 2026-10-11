// SPDX-License-Identifier: GPL-3.0-only
#include "di1/common.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <cstring>
#include <string>

namespace crabe::di1 {
namespace {
    Config settings;
    wchar_t iniPath[32768]{};
    std::wstring directory;
    HANDLE logFile = INVALID_HANDLE_VALUE;
    SRWLOCK logLock = SRWLOCK_INIT;
}
void openLogAndConfig(HMODULE module) {
    wchar_t path[32768]{};
    const DWORD count = GetModuleFileNameW(module, path, 32768);
    if (!count || count >= 32768) return;
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return;
    *(slash + 1) = L'\0';
    directory = path;
    swprintf_s(iniPath, L"%sLiteralCrab.ini", path);
    wchar_t logfile[32768]{};
    swprintf_s(logfile, L"%sLiteralCrab.log", path);
    logFile = CreateFileW(logfile, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    wchar_t mode[32]{};
    GetPrivateProfileStringW(L"Display", L"Mode", L"borderless", mode, 32, iniPath);
    if (!_wcsicmp(mode, L"windowed")) settings.mode = Mode::Windowed;
    else if (!_wcsicmp(mode, L"fullscreen")) settings.mode = Mode::Fullscreen;
    else if (_wcsicmp(mode, L"borderless")) log("Invalid Mode; using borderless.");
    settings.unlockResolutions = GetPrivateProfileIntW(L"Display", L"UnlockResolutions", 1, iniPath) != 0;
    settings.fixUiAspect = GetPrivateProfileIntW(L"Display", L"FixUIAspect", 1, iniPath) != 0;
    settings.fixFov = GetPrivateProfileIntW(L"Display", L"FixFOV", 1, iniPath) != 0;
    settings.screenshotHotkeys = GetPrivateProfileIntW(L"Display", L"ScreenshotHotkeys", 1, iniPath) != 0;
    settings.keepVisibleOnFocusLoss = GetPrivateProfileIntW(L"Display", L"KeepVisibleOnFocusLoss", 1, iniPath) != 0;
    settings.scaleMouse = GetPrivateProfileIntW(L"Display", L"ScaleMouse", 1, iniPath) != 0;
    settings.altEnter = GetPrivateProfileIntW(L"Display", L"AltEnter", 1, iniPath) != 0;
    settings.rememberMode = GetPrivateProfileIntW(L"Display", L"RememberMode", 1, iniPath) != 0;
    wchar_t limit[64]{}, sync[32]{};
    GetPrivateProfileStringW(L"Display", L"FrameLimit", L"120", limit, 64, iniPath);
    wchar_t* end{};
    const long long parsedLimit = _wcstoi64(limit, &end, 10);
    settings.frameLimit = end != limit && *end == L'\0' ? clampFrameLimit(parsedLimit) : 120;
    GetPrivateProfileStringW(L"Display", L"VSync", L"auto", sync, 32, iniPath);
    if (!_wcsicmp(sync,L"off")) settings.vsync = VSync::Off;
    else if (!_wcsicmp(sync,L"on")) settings.vsync = VSync::On;
    else if (_wcsicmp(sync,L"auto")) log("Invalid VSync; using auto.");
    settings.diagnostics = GetPrivateProfileIntW(L"Display", L"Diagnostics", 0, iniPath) != 0;
    settings.mods = GetPrivateProfileIntW(L"Mods", L"Enabled", 1, iniPath) != 0;
    settings.modTrace = GetPrivateProfileIntW(L"Mods", L"Trace", 0, iniPath) != 0;
    wchar_t delay[64]{};
    GetPrivateProfileStringW(L"Camera", L"ReturnDelay", L"default", delay, 64, iniPath);
    if (_wcsicmp(delay, L"default")) {
        wchar_t* delayEnd{};
        const double seconds = wcstod(delay, &delayEnd);
        if (delayEnd != delay && !*delayEnd && seconds >= 0.0 && seconds <= 3600.0)
            settings.cameraReturnDelay = static_cast<float>(seconds);
        else log("Invalid Camera ReturnDelay; using the game's own delays.");
    }
    settings.motionBlur = GetPrivateProfileIntW(L"PostProcessing", L"MotionBlur", 1, iniPath) != 0;
    settings.depthOfField = GetPrivateProfileIntW(L"PostProcessing", L"DepthOfField", 1, iniPath) != 0;
    settings.bloom = GetPrivateProfileIntW(L"PostProcessing", L"Bloom", 1, iniPath) != 0;
    settings.swapXY = GetPrivateProfileIntW(L"Controller", L"SwapXY", 0, iniPath) != 0;
    const auto limitScale = GetPrivateProfileIntW(L"ToyBox", L"LimitScale", 1, iniPath);
    if (limitScale >= 1 && limitScale <= 100) settings.toyBoxLimitScale = limitScale;
    else log("Invalid ToyBox LimitScale; using the game's limits.");
    settings.charactersInAnyPlayset = GetPrivateProfileIntW(L"Gameplay", L"CharactersInAnyPlayset", 0, iniPath) != 0;
    settings.skipIntro = GetPrivateProfileIntW(L"Startup", L"SkipIntro", 0, iniPath) != 0;
    settings.toyBoxShare = GetPrivateProfileIntW(L"ToyBoxes", L"Enabled", 0, iniPath) != 0;
    std::wstring servers(4096, wchar_t{});
    servers.resize(GetPrivateProfileStringW(L"ToyBoxes", L"Servers", L"", servers.data(), 4096, iniPath));
    settings.toyBoxServers = std::move(servers);
    const auto ranged = [&](const wchar_t* key, unsigned fallback, unsigned low, unsigned high) {
        const auto value = GetPrivateProfileIntW(L"ToyBoxes", key, static_cast<INT>(fallback), iniPath);
        if (value >= low && value <= high) return value;
        log("Invalid ToyBoxes %ls; using %u.", key, fallback);
        return fallback;
    };
    settings.toyBoxMaximum = ranged(L"MaxToyBoxes", 5000, 1, 20000);
    settings.toyBoxServerPackageMB = ranged(L"ServerMaxPackageMB", 32, 1, 1024);
    settings.toyBoxServerMirrorMB = ranged(L"ServerMirrorMB", 1024, 16, 65536);
    log("LiteralCrab DI1 ASI " LITERALCRAB_VERSION "; upstream dev 7f251e8eec5de534a2ed9b8efa0694f259706f0f.");
    log("Display mode: %ls; resolution unlock=%d; UI aspect=%d; FOV=%d.",
        modeName(settings.mode), settings.unlockResolutions, settings.fixUiAspect, settings.fixFov);
    log("Frame limit=%u FPS (0=unlimited); VSync=%ls.", settings.frameLimit,
        settings.vsync == VSync::Off ? L"off" : settings.vsync == VSync::On ? L"on" : L"auto");
    if (settings.diagnostics) log("Diagnostics enabled: frame statistics and input state are logged on focus changes.");
}
const Config& config() noexcept { return settings; }
const wchar_t* pluginDirectory() noexcept { return directory.c_str(); }
void saveMode(Mode mode) {
    if (settings.rememberMode && iniPath[0] &&
        !WritePrivateProfileStringW(L"Display", L"Mode", modeName(mode), iniPath))
        log("Could not persist display mode (Win32 %lu).", GetLastError());
}
void log(const char* format, ...) {
    if (logFile == INVALID_HANDLE_VALUE) return;
    char buffer[2048]{};
    SYSTEMTIME now{};
    GetLocalTime(&now);
    int prefix = sprintf_s(buffer, "[%02u:%02u:%02u.%03u] ", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds);
    va_list args;
    va_start(args, format);
    vsnprintf_s(buffer + prefix, sizeof(buffer) - prefix - 3, _TRUNCATE, format, args);
    va_end(args);
    strcat_s(buffer, "\r\n");
    DWORD written{};
    AcquireSRWLockExclusive(&logLock);
    WriteFile(logFile, buffer, static_cast<DWORD>(strlen(buffer)), &written, nullptr);
    ReleaseSRWLockExclusive(&logLock);
}
bool codeMatches(std::uintptr_t address, const char* pattern) noexcept {
    for (std::size_t offset = 0;; ++offset) {
        while (*pattern == ' ') ++pattern;
        if (!*pattern) return true;
        if (!readable(reinterpret_cast<const void*>(address + offset), 1)) return false;
        if (pattern[0] == '?' && pattern[1] == '?') {
            pattern += 2;
            continue;
        }
        char* next{};
        const auto byte = std::strtoul(pattern, &next, 16);
        if (next == pattern || byte > 0xFF || *reinterpret_cast<const unsigned char*>(address + offset) != byte) return false;
        pattern = next;
    }
}
bool callsTo(std::uintptr_t base, std::uint32_t site, std::uint32_t target) noexcept {
    const auto* bytes = reinterpret_cast<const unsigned char*>(base + site);
    return readable(bytes, 5) && bytes[0] == 0xE8 &&
        base + site + 5 + *reinterpret_cast<const std::int32_t*>(bytes + 1) == base + target;
}
bool readable(const void* address, std::size_t length) noexcept {
    auto cursor = reinterpret_cast<std::uintptr_t>(address);
    if (!cursor || length > UINTPTR_MAX - cursor) return false;
    const auto end = cursor + length;
    while (cursor < end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(cursor), &info, sizeof(info)) ||
            info.State != MEM_COMMIT || (info.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
        const auto next = reinterpret_cast<std::uintptr_t>(info.BaseAddress) + info.RegionSize;
        if (next <= cursor) return false;
        cursor = next;
    }
    return true;
}
}
