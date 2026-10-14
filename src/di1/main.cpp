// SPDX-License-Identifier: GPL-3.0-only
#include "di1/camera.hpp"
#include "di1/character_freedom.hpp"
#include "di1/common.hpp"
#include "di1/controller.hpp"
#include "di1/intro.hpp"
#include "di1/display.hpp"
#include "di1/engine.hpp"
#include "di1/mods.hpp"
#include "di1/post_effects.hpp"
#include "di1/toybox_share.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cwchar>

namespace {
    HMODULE self{};
    std::atomic<bool> initialized{};
    // Without DPI awareness Windows scaling (e.g. 150%) virtualizes the game window: a 4K back
    // buffer is shrunk into a 2560x1440 logical client and stretched again by DWM, and mouse
    // mapping gains the same ratio. Must run before the game creates its window.
    void enableDpiAwareness() {
        using SetContext = BOOL (WINAPI*)(HANDLE);
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        const auto setContext = user32 ? reinterpret_cast<SetContext>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")) : nullptr;
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 = -4 (Windows 10 1703+).
        if (setContext && setContext(reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4))))
            crabe::di1::log("DPI awareness: per-monitor v2; the game window uses physical pixels.");
        else if (setContext && GetLastError() == ERROR_ACCESS_DENIED)
            crabe::di1::log("DPI awareness already set for this process (manifest or compatibility setting).");
        else if (SetProcessDPIAware())
            crabe::di1::log("DPI awareness: system aware.");
        else crabe::di1::log("DPI awareness could not be enabled (Win32 %lu).", GetLastError());
    }
    void initialize() {
        if (initialized.exchange(true)) return;
        crabe::di1::openLogAndConfig(self);
        HMODULE host = GetModuleHandleW(nullptr);
        wchar_t path[32768]{};
        const auto length = GetModuleFileNameW(host, path, 32768);
        const wchar_t* filename = wcsrchr(path, L'\\');
        if (!length || length >= 32768 || !filename || _wcsicmp(filename + 1, L"DisneyInfinity1.exe") ||
            !crabe::di1::supportedBuild(host)) {
            crabe::di1::log("REFUSED: executable is not the measured Disney Infinity 1.0 Gold PC build. No hooks installed.");
            return;
        }
        enableDpiAwareness();
        const auto status = MH_Initialize();
        if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED) {
            crabe::di1::log("MinHook initialization refused: %s.", MH_StatusToString(status)); return;
        }
        if (!crabe::di1::installEngineHooks(host)) {
            crabe::di1::removeEngineHooks();
            crabe::di1::log("REFUSED: display profile validation failed. No hooks installed."); return;
        }
        if (!crabe::di1::installDisplayHooks(host, crabe::di1::config())) {
            crabe::di1::removeEngineHooks();
            crabe::di1::log("REFUSED: early D3D9 interception failed. No hooks installed."); return;
        }
        // The hooks outlive initialization. Pin the ASI so a loader cannot unload live detours.
        HMODULE pinned{};
        GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
            reinterpret_cast<LPCWSTR>(&initialize), &pinned);
        crabe::di1::log("READY: DI1 display hooks installed before engine startup.");
        // Optional and independent: only installed when LiteralCrab.ini sets a camera option.
        crabe::di1::installCameraHooks(host, crabe::di1::config());
        crabe::di1::installPostEffectHooks(host, crabe::di1::config());
        crabe::di1::installControllerHooks(host, crabe::di1::config());
        crabe::di1::installIntroSkip(host, crabe::di1::config());
        // Both halves or neither: ownership/FX data needs the script loader.
        if (crabe::di1::config().charactersInAnyPlayset && !crabe::di1::config().mods)
            crabe::di1::log("Gameplay: CharactersInAnyPlayset needs [Mods] Enabled=1; nothing changed.");
        else
            crabe::di1::installCharacterFreedom(host, crabe::di1::config());
        // Before the mods: its Toy Box menu entry is a script patch the mod loader adds.
        crabe::di1::installToyBoxShare(host, crabe::di1::config());
        // Independent of the display hooks: a refused mod loader leaves game scripts untouched.
        if (!crabe::di1::installMods(host)) crabe::di1::removeCharacterFreedom();
    }
}
extern "C" void WINAPI InitializeASI() { initialize(); }
BOOL WINAPI DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = module;
        // Fallback for LoadLibrary-only ASI loaders; UAL's InitializeASI call is idempotent.
        // Only verified memory/import hooks are installed here; D3D9 loads lazily in create9.
        initialize();
    }
    // OS process teardown releases the pinned module. No thread suspension in loader-lock detach.
    return TRUE;
}
