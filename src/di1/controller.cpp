// SPDX-License-Identifier: GPL-3.0-only
// [Controller] SwapXY: exchange the X and Y buttons of XInput controllers.
//
// DI1 reads Xbox-compatible controllers only through XInputGetState, imported from
// XINPUT9_1_0.dll. The game's import is redirected to a wrapper that swaps the two button bits
// in the state the game receives, so every action, menu and binding sees the swapped layout.
// On-screen prompts still show the game's original button letters.
#include "di1/controller.hpp"
#include "di1/imports.hpp"
#include <atomic>

namespace crabe::di1 {
namespace {
    constexpr WORD buttonX = 0x4000, buttonY = 0x8000;  // XINPUT_GAMEPAD_X / XINPUT_GAMEPAD_Y

    // XINPUT_STATE without including xinput.h, whose library the ASI does not link.
    struct Gamepad { WORD buttons; BYTE leftTrigger, rightTrigger; SHORT thumbLX, thumbLY, thumbRX, thumbRY; };
    struct State { DWORD packet; Gamepad gamepad; };
    using GetStateFn = DWORD(WINAPI*)(DWORD, State*);

    ImportPatch getStateImport;
    bool swapButtons{}, traceInput{};
    std::atomic<DWORD> previous[4] = {~DWORD{}, ~DWORD{}, ~DWORD{}, ~DWORD{}};
    std::atomic<unsigned> transitions{};

    DWORD WINAPI getState(DWORD user, State* state) {
        const DWORD result = reinterpret_cast<GetStateFn>(getStateImport.original)(user, state);
        const DWORD savedError = GetLastError();
        if (traceInput && user < 4 && previous[user].exchange(result, std::memory_order_relaxed) != result) {
            const unsigned event = transitions.fetch_add(1, std::memory_order_relaxed);
            if (event < 128) log("Controller trace: user=%lu XInputGetState result=%lu connected=%d tick=%lu.",
                user, result, result == ERROR_SUCCESS, GetTickCount());
            if (event == 127) log("Controller trace: transition limit reached; further changes omitted.");
        }
        if (swapButtons && result == ERROR_SUCCESS && state) {
            const WORD buttons = state->gamepad.buttons;
            const WORD swapped = static_cast<WORD>((buttons & buttonX ? buttonY : 0) | (buttons & buttonY ? buttonX : 0));
            state->gamepad.buttons = static_cast<WORD>((buttons & ~(buttonX | buttonY)) | swapped);
        }
        SetLastError(savedError);
        return result;
    }
}

bool installControllerHooks(HMODULE host, const Config& options) {
    if (!options.swapXY && !options.traceCharacterFreedom && !options.diagnostics) return true;
    swapButtons = options.swapXY;
    traceInput = options.traceCharacterFreedom || options.diagnostics;
    transitions = 0;
    for (auto& status : previous) status = ~DWORD{};
    if (!getStateImport.install(host, "XINPUT9_1_0.dll", "XInputGetState", reinterpret_cast<void*>(&getState))) {
        log("Controller: XInputGetState import not found; buttons stay as the game sets them.");
        return false;
    }
    if (swapButtons) log("Controller: X and Y buttons swapped.");
    if (traceInput) log("Controller: read-only XInput connection transitions enabled (128 events).");
    return true;
}
}
