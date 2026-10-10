// SPDX-License-Identifier: GPL-3.0-only
#include "di1/common.hpp"
#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <cstring>

namespace crabe::di1 {
namespace {
    Config settings;
    wchar_t iniPath[32768]{};
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
    log("LiteralCrab DI1 ASI 0.1.0; upstream dev 7f251e8eec5de534a2ed9b8efa0694f259706f0f.");
    log("Display mode: %ls; resolution unlock=%d; UI aspect=%d; FOV=%d.",
        modeName(settings.mode), settings.unlockResolutions, settings.fixUiAspect, settings.fixFov);
    log("Frame limit=%u FPS (0=unlimited); VSync=%ls.", settings.frameLimit,
        settings.vsync == VSync::Off ? L"off" : settings.vsync == VSync::On ? L"on" : L"auto");
}
const Config& config() noexcept { return settings; }
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
