// SPDX-License-Identifier: GPL-3.0-only
#include "di1/common.hpp"
#include "di1/display.hpp"
#include "di1/engine.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cwchar>

namespace {
    HMODULE self{};
    std::atomic<bool> initialized{};
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
        crabe::di1::log("READY: DI1 display hooks installed before engine startup. Gameplay/characters untouched.");
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
