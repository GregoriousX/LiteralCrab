// SPDX-License-Identifier: GPL-3.0-only
// [Controller] SwapXY: exchange the X and Y buttons of XInput controllers.
//
// DI1 reads Xbox-compatible controllers only through XInputGetState, imported from
// XINPUT9_1_0.dll. The game's import is redirected to a wrapper that swaps the two button bits
// in the state the game receives, so every action, menu and binding sees the swapped layout.
// On-screen prompts still show the game's original button letters.
#include "di1/controller.hpp"
#include "di1/imports.hpp"

namespace crabe::di1 {
namespace {
    constexpr WORD buttonX = 0x4000, buttonY = 0x8000;  // XINPUT_GAMEPAD_X / XINPUT_GAMEPAD_Y

    // XINPUT_STATE without including xinput.h, whose library the ASI does not link.
    struct Gamepad { WORD buttons; BYTE leftTrigger, rightTrigger; SHORT thumbLX, thumbLY, thumbRX, thumbRY; };
    struct State { DWORD packet; Gamepad gamepad; };
    using GetStateFn = DWORD(WINAPI*)(DWORD, State*);

    ImportPatch getStateImport;

    DWORD WINAPI getState(DWORD user, State* state) {
        const DWORD result = reinterpret_cast<GetStateFn>(getStateImport.original)(user, state);
        if (result == ERROR_SUCCESS && state) {
            const WORD buttons = state->gamepad.buttons;
            const WORD swapped = static_cast<WORD>((buttons & buttonX ? buttonY : 0) | (buttons & buttonY ? buttonX : 0));
            state->gamepad.buttons = static_cast<WORD>((buttons & ~(buttonX | buttonY)) | swapped);
        }
        return result;
    }
}

bool installControllerHooks(HMODULE host, const Config& options) {
    if (!options.swapXY) return true;
    if (!getStateImport.install(host, "XINPUT9_1_0.dll", "XInputGetState", reinterpret_cast<void*>(&getState))) {
        log("Controller: XInputGetState import not found; buttons stay as the game sets them.");
        return false;
    }
    log("Controller: X and Y buttons swapped.");
    return true;
}
}
