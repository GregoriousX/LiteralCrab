// SPDX-License-Identifier: GPL-3.0-only
// Engine feature "TriggerGuiScreen": the world graph node of that name opens the menu its
// ScreenCommand names, as in the console builds.
//
// The PC build still parses ScreenCommand into the node (0-5) but its In handler only checks
// that the triggering player is local and fires Out. The Wii U handler instead runs:
//   0  Toy Box editor ribbon         GameFrontEndLayer::CreateRibbonScreen(player)
//   1  "Adventures"                  after SetAdventureCancelReturnsToPauseMenu(false, player)
//   2  Toy Vault spinner             RumpusUnlockUI::ShowSpinnerPrompt(host, 0)
//   3  "Catalog"   4  "Collection"   5  "PauseTravel" (also clears the cancel flag)
// Screens are added with the same front-end call and arguments on both platforms. Every PC
// function used here is the one the PC game itself uses for that action: CreateRibbonScreen
// and its gates from the PC editor input handler, the Toy Vault from UI_ActivateToyMaker, the
// cancel flag from UI_SetAdventureCancelReturnsToPauseMenu.
//
// On PC these menus normally sit on the pause menu, whose pause switches the mouse to a pointer
// and whose unpause switches it back to camera control. Opened from the world they would keep
// the camera mouse, so a menu opened here switches to the pointer as UI_MousePointerMode does,
// and the camera mouse returns once the player has had no screen for a few input frames.
#include "di1/gui_screens.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace crabe::di1 {
namespace {
    // Meridian::TriggerGuiScreenNode: In handler, the vtable slot that reads ScreenCommand.
    constexpr std::uint32_t handlerRva = 0x966EF0, readCommandRva = 0x976120, readCommandSlot = 0xF5E518;
    constexpr std::uint32_t hasInLinksRva = 0x2910C0, fetchRva = 0x29EFC0, fireRva = 0x2A2110, playerCountRva = 0xAD85E0;
    constexpr std::size_t commandOffset = 0x30, playerFetch = 0x28, outImpulse = 0x30;
    // GameFrontEndLayer: its vtable, CreateRibbonScreen(int), UI_SetAdventureCancelReturnsToPauseMenu.
    constexpr std::uint32_t frontEndVtableRva = 0xF47BCC, ribbonRva = 0x837230, cancelSetterRva = 0x831C20;
    constexpr std::size_t addScreenSlot = 0x6C, playerHasScreenSlot = 0x8C, cancelFlag = 0x3E29;
    // PC editor input handler: the native ribbon entry and its gates.
    constexpr std::uint32_t editorInputRva = 0x7C81C0, worldFlagRva = 0xAFE260, toyBoxEditableRva = 0xC56BA0, playerBusyRva = 0x755D30;
    // UI_ActivateToyMaker and RumpusUnlockUI::ShowSpinnerPrompt; the first local player.
    constexpr std::uint32_t toyMakerRva = 0x836EA0, spinnerRva = 0x924550, primaryPlayerRva = 0xAE1350;
    // Mouse modes: UI_MouseCamMode/UI_MousePointerMode are front-end slots 0xC0/0xC4 over the
    // WindowsSystemInputDriver, whose slot 0x40 reports camera control and slot 0x04 updates it
    // once per frame.
    constexpr std::uint32_t camModeRva = 0x832090, pointerModeRva = 0x8320C0, hasScreenRva = 0x832C50, addScreenRva = 0xC48EA0;
    constexpr std::uint32_t inputVtableRva = 0xE9BAD4, inputUpdateRva = 0x97150, inputCapturedRva = 0x93660;
    constexpr std::size_t camModeSlot = 0xC0, pointerModeSlot = 0xC4, inputUpdateSlot = 0x04, inputCapturedSlot = 0x40;
    constexpr unsigned restoreFrames = 3;
    constexpr unsigned maxLogLines = 200;

    using HandlerFn = int(__thiscall*)(void*, void*);
    using HasInLinksFn = bool(__thiscall*)(void*, void*);
    using FetchFn = bool(__thiscall*)(void*, void*, int*);
    using FireFn = void(__thiscall*)(void*, void*, void*);
    using PlayerFn = int(__thiscall*)(void*);
    using AddScreenFn = int(__thiscall*)(void*, const char*, int, int, int, const char*, int, int, int);
    using PlayerQueryFn = bool(__thiscall*)(void*, int);
    using RibbonFn = void(__thiscall*)(void*, int);
    using WorldFlagFn = bool(__thiscall*)(void*, const char*, std::uint32_t, int, int);
    using FlagFn = bool(__thiscall*)(void*);
    using SpinnerFn = void(__thiscall*)(void*, int, int);
    using VoidFn = void(__thiscall*)(void*);

    std::uintptr_t base{};
    HandlerFn original{};
    HasInLinksFn hasInLinks{};
    FetchFn fetch{};
    FireFn fire{};
    PlayerFn playerCount{}, primaryPlayer{};
    RibbonFn createRibbon{};
    WorldFlagFn worldFlag{};
    FlagFn toyBoxEditable{};
    PlayerQueryFn playerBusy{};
    SpinnerFn showSpinner{};
    // Globals, read from the game's own instructions because the executable is relocated.
    void** playerManager{};
    void** frontEnd{};
    void** screenLayer{};
    void** worldData{};
    void** toyBox{};
    void** busyTracker{};
    void** rumpusUnlock{};
    const char* mainLayer{};
    const char* worldIsPlayset{};
    void** inputDriver{};
    VoidFn originalInputUpdate{};
    std::atomic<unsigned> logged{};
    // Pointer mode switched on for this player's menu; -1 when nothing is to be restored.
    // Written and read on the thread that runs world graphs and menus.
    int restorePlayer = -1;
    unsigned framesWithoutScreen{};
    DWORD menuThread{};
    bool threadWarned{};

    template <class T> T global(std::uint32_t operand) { return *reinterpret_cast<T*>(base + operand); }

    void note(int command, int player, const char* outcome) {
        if (logged.fetch_add(1, std::memory_order_relaxed) < maxLogLines)
            log("TriggerGuiScreen: command %d, player %d: %s.", command, player, outcome);
    }

    bool resolved() {
        // In handler and ScreenCommand reader (0-5 stored at +0x30), and the reader's vtable slot.
        if (!codeMatches(base + handlerRva,
                "51 8B 44 24 08 53 55 8B 28 56 57 8B F9 33 F6 57 8D 4D 28 89 74 24 14 E8 B4 A1 92 FF 84 C0 74 12 "
                "8D 4C 24 10 51 57 8D 4D 28 E8 A2 80 93 FF 8B 74 24 10 8B 0D ?? ?? ?? ?? 8B F9 85 F6 78 30 E8 AD 16 17 00 "
                "3B F0 7D 27 8B 97 E8 00 00 00 C1 E6 06 83 7C 32 08 01 0F 94 C0 84 C0 74 12 8B 44 24 18 8B 48 04 6A 00 "
                "51 8D 4D 30 E8 B2 B1 93 FF 5F 5E 5D 33 C0 5B 59 C2 04 00") ||
            !codeMatches(base + readCommandRva, "8B 44 24 04 53 8B 18 56 57 68 ?? ?? ?? ?? 8B F1 E8 2B 69 3B 00 83 C4 04 50 6A 00 8B CB "
                "E8 FE 30 75 FF 56 8D 4E 24 89 46 2C E8 C2 3E 92 FF 8B 46 2C 83 F8 05 77 4F FF 24 85") ||
            !codeMatches(base + readCommandRva + 0x3E, "C7 46 30 00 00 00 00") ||
            !readable(reinterpret_cast<void*>(base + readCommandSlot), 4) ||
            global<std::uintptr_t>(readCommandSlot) != base + readCommandRva ||
            !callsTo(base, handlerRva + 0x17, hasInLinksRva) || !callsTo(base, handlerRva + 0x29, fetchRva) ||
            !callsTo(base, handlerRva + 0x3E, playerCountRva) || !callsTo(base, handlerRva + 0x69, fireRva))
            return false;
        // CreateRibbonScreen(int): adds its screen through the front end's vtable +0x6C.
        if (!codeMatches(base + ribbonRva, "57 8B 7C 24 08 57 E8 55 C0 FF FF 84 C0 75 41 57 E8 CB B8 2A 00 83 C4 04 84 C0 74 34 "
                "8B 0D ?? ?? ?? ?? 53 56 8B 35 ?? ?? ?? ?? 8B 1E 6A FF 6A 01 6A 00 68 ?? ?? ?? ?? 6A 00 6A 00 57 57 E8 7E BA FF FF "
                "50 8B 43 6C 8B CE FF D0 5E 5B 5F C2 04 00") ||
            !codeMatches(base + cancelSetterRva, "53 56 57 8B 7C 24 10 6A 02 57 8B F1 E8 8F 43 83 FF 6A 01 57 8B D8 E8 A5 5D 28 00 "
                "83 C4 10 85 C0 0F 95 C0 88 84 33 29 3E 00 00 5F 5E 33 C0 5B C2 04 00"))
            return false;
        // The PC editor input handler: local player, (play set world or editable Toy Box), player
        // not busy, no screen open for the player, then CreateRibbonScreen.
        if (!codeMatches(base + editorInputRva,
                "53 8B D9 8B 43 08 8B 40 04 8B 48 04 56 51 8B 0D ?? ?? ?? ?? E8 97 FE 31 00 8B F0 85 F6 0F 88 AC 00 00 00 "
                "8B 0D ?? ?? ?? ?? E8 F2 03 31 00 3B F0 0F 8D 99 00 00 00 8B 0D ?? ?? ?? ?? 57 8B F9 E8 DC 03 31 00 3B F0 "
                "0F 8D 82 00 00 00 8B 87 E8 00 00 00 8B D6 C1 E2 06 83 7C 10 08 01 0F 94 C0 84 C0 74 6B 8B 0D ?? ?? ?? ?? "
                "8B 81 54 01 00 00 6A 00 6A 00 50 68 ?? ?? ?? ?? E8 22 60 33 00 84 C0 75 0F 8B 0D ?? ?? ?? ?? E8 53 E9 48 00 "
                "84 C0 74 3D 8B 4B 08 83 B9 50 02 00 00 00 75 31 8B 0D ?? ?? ?? ?? 56 E8 C7 DA F8 FF 84 C0 75 21 "
                "8B 0D ?? ?? ?? ?? 8B 11 8B 82 8C 00 00 00 56 FF D0 84 C0 75 0C 8B 0D ?? ?? ?? ?? 56 E8 A2 EF 06 00 "
                "5F 5E 5B C2 0C 00") ||
            !codeMatches(base + toyBoxEditableRva, "8A 81 4E 01 00 00 C3") ||
            !codeMatches(base + worldFlagRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89") ||
            !codeMatches(base + playerBusyRva, "8B 44 24 04 56 50 8B F1 E8 A3 F9 FF FF 83 F8 FF"))
            return false;
        // The Toy Vault: UI_ActivateToyMaker(player) runs ShowSpinnerPrompt(player, 0).
        if (!codeMatches(base + toyMakerRva, "8B 44 24 04 6A 01 50 E8 14 F1 82 FF 83 C4 08 33 C9 51 8B 0D ?? ?? ?? ?? 50 E8 92 D6 0E 00") ||
            !codeMatches(base + spinnerRva, "6A FF 64 A1 00 00 00 00 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 83 EC 08 53 55 56 33 DB 57 8B F1") ||
            !codeMatches(base + primaryPlayerRva, "51 53 56 57 8B F9 C7 44 24 0C FF FF FF FF 33 F6 E8 7B 72 FF") ||
            !callsTo(base, 0xAE4937, primaryPlayerRva))
            return false;
        // Mouse modes and the input driver's frame update, with their vtable slots.
        if (!codeMatches(base + camModeRva, "8B 0D ?? ?? ?? ?? 8B 01 8B 50 40 FF D2 84 C0 75 1C 8B 0D ?? ?? ?? ?? 8B 01 8B 50 44 FF D2 "
                "8B 0D ?? ?? ?? ?? 8B 01 8B 50 4C 6A 00 FF D2 C3") ||
            !codeMatches(base + pointerModeRva, "8B 0D ?? ?? ?? ?? 8B 01 8B 50 40 FF D2 84 C0 74 1C 8B 0D ?? ?? ?? ?? 8B 01 8B 50 48 FF D2 "
                "8B 0D ?? ?? ?? ?? 8B 01 8B 50 4C 6A 01 FF D2 C3") ||
            !codeMatches(base + inputUpdateRva, "A1 ?? ?? ?? ?? 83 EC 10 53 56 8B F1 8B C8 40 33 DB 57 A3") ||
            !codeMatches(base + inputCapturedRva, "8A 81 1E 02 00 00 C3") ||
            !codeMatches(base + hasScreenRva, "53 56 8B 74 24 0C 57 56 8B F9 32 DB E8"))
            return false;
        const auto slot = [](std::uint32_t table, std::size_t offset) { return global<std::uintptr_t>(table + static_cast<std::uint32_t>(offset)) - base; };
        if (slot(frontEndVtableRva, camModeSlot) != camModeRva || slot(frontEndVtableRva, pointerModeSlot) != pointerModeRva ||
            slot(frontEndVtableRva, playerHasScreenSlot) != hasScreenRva || slot(frontEndVtableRva, addScreenSlot) != addScreenRva ||
            slot(inputVtableRva, inputUpdateSlot) != inputUpdateRva || slot(inputVtableRva, inputCapturedSlot) != inputCapturedRva)
            return false;
        inputDriver = global<void**>(camModeRva + 2);
        if (inputDriver != global<void**>(pointerModeRva + 2) || !readable(inputDriver, 4)) return false;

        playerManager = global<void**>(handlerRva + 0x34);
        frontEnd = global<void**>(ribbonRva + 0x1E);
        screenLayer = global<void**>(ribbonRva + 0x26);
        mainLayer = global<const char*>(ribbonRva + 0x33);
        worldData = global<void**>(editorInputRva + 0x65);
        worldIsPlayset = global<const char*>(editorInputRva + 0x75);
        toyBox = global<void**>(editorInputRva + 0x84);
        busyTracker = global<void**>(editorInputRva + 0x9F);
        rumpusUnlock = global<void**>(toyMakerRva + 0x14);
        // The same globals appear in both handlers; a mismatch means a different build.
        if (playerManager != global<void**>(editorInputRva + 0x10) || frontEnd != global<void**>(editorInputRva + 0xC4) ||
            screenLayer != global<void**>(editorInputRva + 0xAF) ||
            !readable(playerManager, 4) || !readable(frontEnd, 4) || !readable(screenLayer, 4) || !readable(worldData, 4) || !readable(toyBox, 4) ||
            !readable(busyTracker, 4) || !readable(rumpusUnlock, 4) || !readable(mainLayer, 5) || !readable(worldIsPlayset, 15) ||
            std::memcmp(mainLayer, "MAIN", 5) || std::memcmp(worldIsPlayset, "worldIsPlayset", 15))
            return false;

        hasInLinks = reinterpret_cast<HasInLinksFn>(base + hasInLinksRva);
        fetch = reinterpret_cast<FetchFn>(base + fetchRva);
        fire = reinterpret_cast<FireFn>(base + fireRva);
        playerCount = reinterpret_cast<PlayerFn>(base + playerCountRva);
        primaryPlayer = reinterpret_cast<PlayerFn>(base + primaryPlayerRva);
        createRibbon = reinterpret_cast<RibbonFn>(base + ribbonRva);
        worldFlag = reinterpret_cast<WorldFlagFn>(base + worldFlagRva);
        toyBoxEditable = reinterpret_cast<FlagFn>(base + toyBoxEditableRva);
        playerBusy = reinterpret_cast<PlayerQueryFn>(base + playerBusyRva);
        showSpinner = reinterpret_cast<SpinnerFn>(base + spinnerRva);
        return true;
    }

    // The front end, only while it is the GameFrontEndLayer these offsets were measured on. The
    // game keeps it in two globals (the screen stack and the front end, filled from the first).
    void* frontEndLayer() {
        void* layer = *frontEnd;
        return layer && layer == *screenLayer && *static_cast<std::uintptr_t*>(layer) == base + frontEndVtableRva ? layer : nullptr;
    }

    template <class T> T virtualFunction(void* object, std::size_t slot) {
        return *reinterpret_cast<T*>(*static_cast<std::uintptr_t*>(object) + slot);
    }

    bool localPlayer(void* manager, int player) {
        if (!manager || player < 0 || player >= playerCount(manager)) return false;
        const auto* players = *reinterpret_cast<const unsigned char* const*>(static_cast<const unsigned char*>(manager) + 0xE8);
        return *reinterpret_cast<const int*>(players + player * 64 + 8) == 1;
    }

    // The input driver, only while it is the WindowsSystemInputDriver measured here.
    void* input() {
        void* driver = *inputDriver;
        return driver && *static_cast<std::uintptr_t*>(driver) == base + inputVtableRva ? driver : nullptr;
    }
    bool cameraMouse(void* driver) { return virtualFunction<FlagFn>(driver, inputCapturedSlot)(driver); }

    // A menu opened from the world: give the mouse to the menu, remember to give it back.
    void usePointer(void* layer, int player) {
        void* driver = input();
        if (!driver || !cameraMouse(driver)) return;
        virtualFunction<VoidFn>(layer, pointerModeSlot)(layer);
        restorePlayer = player;
        framesWithoutScreen = 0;
        menuThread = GetCurrentThreadId();
    }

    // Once per input frame: the camera mouse returns after the player's menus are all closed.
    void restoreCamera() noexcept {
        __try {
            if (GetCurrentThreadId() != menuThread) {
                if (!threadWarned) {
                    threadWarned = true;
                    log("TriggerGuiScreen: input updates run on another thread; the camera mouse is not restored automatically.");
                }
                return;
            }
            void* layer = frontEndLayer();
            if (!layer || virtualFunction<PlayerQueryFn>(layer, playerHasScreenSlot)(layer, restorePlayer)) {
                framesWithoutScreen = 0;
                return;
            }
            if (++framesWithoutScreen < restoreFrames) return;
            void* driver = input();
            if (driver && !cameraMouse(driver)) virtualFunction<VoidFn>(layer, camModeSlot)(layer);
            if (logged.fetch_add(1, std::memory_order_relaxed) < maxLogLines)
                log("TriggerGuiScreen: player %d closed the menus; camera mouse restored.", restorePlayer);
            restorePlayer = -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            restorePlayer = -1;
            log("TriggerGuiScreen: native fault 0x%08lX while restoring the camera mouse.", GetExceptionCode());
        }
    }

    void __fastcall hookInputUpdate(void* driver, void*) {
        originalInputUpdate(driver);
        if (restorePlayer >= 0) restoreCamera();
    }

    // Adds a front-end screen for the player exactly as the console handler does.
    bool addScreen(void* layer, const char* name, int player) {
        return virtualFunction<AddScreenFn>(layer, addScreenSlot)(layer, name, player, 0, 0, mainLayer, 0, 1, -1) != -1;
    }

    // Runs the command. False when it was refused; the console handler then skips Out as well.
    bool run(int command, int player) {
        void* layer = frontEndLayer();
        if (!layer) {
            note(command, player, "front end unavailable, ignored");
            return false;
        }
        // One menu at a time: the PC editor input applies the same check before its ribbon.
        if (command != 2 && virtualFunction<PlayerQueryFn>(layer, playerHasScreenSlot)(layer, player)) {
            note(command, player, "player already has a screen open, ignored");
            return false;
        }
        static const char* const screens[] = {nullptr, "Adventures", nullptr, "Catalog", "Collection", "PauseTravel"};
        switch (command) {
        case 0: {
            void* world = *worldData;
            void* box = *toyBox;
            void* busy = *busyTracker;
            const bool editable = (world && worldFlag(world, worldIsPlayset, *reinterpret_cast<std::uint32_t*>(
                static_cast<unsigned char*>(world) + 0x154), 0, 0)) || (box && toyBoxEditable(box));
            if (!editable || !busy || playerBusy(busy, player)) {
                note(command, player, "editor not available here, ignored");
                return false;
            }
            createRibbon(layer, player);
            note(command, player, "editor ribbon requested");
            return true;
        }
        case 1:
        case 5:
            *(static_cast<unsigned char*>(layer) + cancelFlag + player) = 0;
            [[fallthrough]];
        case 3:
        case 4: {
            const bool added = addScreen(layer, screens[command], player);
            note(command, player, added ? screens[command] : "screen refused by the front end");
            if (added) usePointer(layer, player);
            return true;
        }
        default:
            return true;
        }
    }

    // The Toy Vault belongs to the host, like on console; Out fires whether or not it opened.
    void runToyVault(void* manager) {
        const int host = primaryPlayer(manager);
        void* vault = *rumpusUnlock;
        if (vault && localPlayer(manager, host)) {
            showSpinner(vault, host, 0);
            note(2, host, "Toy Vault requested");
        } else {
            note(2, host, "no local host, ignored");
        }
    }

    int handle(void* node, void* event) noexcept {
        __try {
            const int command = *reinterpret_cast<const int*>(static_cast<unsigned char*>(node) + commandOffset);
            if (command < 0 || command > 5) return original(node, event);
            auto* definition = *static_cast<unsigned char**>(event);
            int player = 0;
            if (hasInLinks(definition + playerFetch, node)) fetch(definition + playerFetch, node, &player);
            void* manager = *playerManager;
            if (!localPlayer(manager, player)) return 0;
            if (command == 2) {
                runToyVault(manager);
            } else if (!run(command, player)) {
                return 0;
            }
            fire(definition + outImpulse, static_cast<void**>(event)[1], nullptr);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            if (logged.fetch_add(1, std::memory_order_relaxed) < maxLogLines)
                log("TriggerGuiScreen: native fault 0x%08lX; command ignored.", GetExceptionCode());
        }
        return 0;
    }

    int __fastcall hookIn(void* node, void*, void* event) { return handle(node, event); }
}

bool installGuiScreenCommands(HMODULE host) {
    base = reinterpret_cast<std::uintptr_t>(host);
    if (!resolved()) {
        log("Mods: engine feature TriggerGuiScreen unavailable: game code differs.");
        return false;
    }
    void* targets[] = {reinterpret_cast<void*>(base + handlerRva), reinterpret_cast<void*>(base + inputUpdateRva)};
    if (MH_CreateHook(targets[0], reinterpret_cast<void*>(&hookIn), reinterpret_cast<void**>(&original)) != MH_OK ||
        MH_CreateHook(targets[1], reinterpret_cast<void*>(&hookInputUpdate), reinterpret_cast<void**>(&originalInputUpdate)) != MH_OK ||
        MH_EnableHook(targets[0]) != MH_OK || MH_EnableHook(targets[1]) != MH_OK) {
        for (const auto target : targets) { MH_DisableHook(target); MH_RemoveHook(target); }
        log("Mods: engine feature TriggerGuiScreen unavailable: hook refused.");
        return false;
    }
    log("Mods: engine feature TriggerGuiScreen enabled; world graph nodes open their menus.");
    return true;
}
}
