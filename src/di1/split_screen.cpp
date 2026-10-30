// SPDX-License-Identifier: GPL-3.0-only
// [SplitScreen] Enabled: local two-player drop-in, as on the consoles.
//
// The PC build keeps the console engine's second local player: GameSpecificInit::MaxLocalPlayers
// still returns 2, Players keeps two viewports, and GameStandAloneLoop still has
// HandleAddingRuntimePlayer (vtable +0xAC), which checks Players::IsPlayerDropInEnabled and
// queues the controller as a new player. Only the caller was cut: on the consoles
// StandAloneLoop::InterceptControls turned START on a controller that belongs to no player into
// that call, while PC's GameStandAloneLoop::InterceptControls (vtable +0xB4) handles debug
// menus and then lets every controller's START pause the game.
//
// The option replaces that one vtable slot. Before the game's own handling it restores the
// console branch: a connected gamepad (never the keyboard) that is not mapped to a player and
// presses START while fewer than two local players exist is handed to HandleAddingRuntimePlayer.
// The same press is kept from also pausing the game. Everything after (the drop-in popup,
// portal figure, avatar spawn and split viewports) is the game's own code.
#include "di1/split_screen.hpp"
#include <cstdint>
#include <cstring>

namespace crabe::di1 {
namespace {
    // GameStandAloneLoop vtable slots: InterceptControls (+0xB4), HandleAddingRuntimePlayer (+0xAC).
    constexpr std::uint32_t interceptRva = 0x6022B0, interceptSlot = 0xF50828;
    constexpr std::uint32_t addPlayerRva = 0x61F4F0, addPlayerSlot = 0xF50820;
    // Globals: WindowsSystemInputDriver and Players.
    constexpr std::uint32_t inputGlobal = 0x121D060, playersGlobal = 0x135766C;
    constexpr std::size_t localPlayers = 0x9C;  // Players: count of local players
    constexpr unsigned maxLocalPlayers = 2;
    constexpr int startButton = 0xD;            // XInputInputDriver button index of START

    // WindowsSystemInputDriver: device count, device by index, player owning a device (-1: none).
    // Input devices: connected, name, pressed/released edges of a button.
    struct InputVtable { void* unused[0x60 / 4]; unsigned (__thiscall* count)(void*); void* unused2[(0x88 - 0x64) / 4];
        void* (__thiscall* device)(void*, unsigned); void* unused3[(0x9C - 0x8C) / 4]; int (__thiscall* playerOf)(void*, unsigned); };
    struct DeviceVtable { void* unused[2]; const char* (__thiscall* name)(void*); void* unused2;
        bool (__thiscall* connected)(void*); void* unused3; bool (__thiscall* pressed)(void*, int, bool);
        bool (__thiscall* released)(void*, int, bool); };

    using InterceptFn = void(__thiscall*)(void*);
    using AddPlayerFn = void(__thiscall*)(void*, unsigned);
    InterceptFn originalIntercept{};
    std::uintptr_t base{};
    // Devices whose join press must not reach the game's START handling (pause on release).
    bool swallowRelease[11]{};
    unsigned joinsLogged{};

    template <class T> T global(std::uint32_t rva) { return *reinterpret_cast<T*>(base + rva); }

    unsigned localPlayerCount() {
        auto* players = global<unsigned char*>(playersGlobal);
        return players ? *reinterpret_cast<unsigned*>(players + localPlayers) : maxLocalPlayers;
    }

    // True when this frame's START handling must be skipped for a device that just joined.
    bool interceptJoin(void* loop) {
        void* input = global<void*>(inputGlobal);
        if (!input) return false;
        const auto* inputCalls = *static_cast<InputVtable* const*>(input);
        const unsigned devices = inputCalls->count(input);
        bool skip = false;
        for (unsigned index = 0; index < devices && index < 11; ++index) {
            void* device = inputCalls->device(input, index);
            if (!device) continue;
            const auto* calls = *static_cast<DeviceVtable* const*>(device);
            if (swallowRelease[index]) {
                if (calls->released(device, startButton, true) || !calls->connected(device)) {
                    swallowRelease[index] = false;
                    skip = true;
                }
                continue;
            }
            if (!calls->connected(device) || !_stricmp(calls->name(device), "Keyboard") ||
                inputCalls->playerOf(input, index) >= 0 || !calls->pressed(device, startButton, true)) continue;
            const unsigned before = localPlayerCount();
            if (before >= maxLocalPlayers) continue;
            // HandleAddingRuntimePlayer refuses with the game's own "game full"/DropInBlocked paths.
            (*static_cast<AddPlayerFn* const*>(loop))[0xAC / 4](loop, index);
            swallowRelease[index] = true;
            if (joinsLogged < 32) {
                ++joinsLogged;
                char owners[64]{};
                for (unsigned other = 0, used = 0; other < devices && other < 11 && used + 4 < sizeof owners; ++other)
                    used += static_cast<unsigned>(wsprintfA(owners + used, " %d", inputCalls->playerOf(input, other)));
                log("SplitScreen: START on device %u (%s) asked to join; local players %u -> %u; device owners:%s.",
                    index, calls->name(device), before, localPlayerCount(), owners);
            }
        }
        return skip;
    }

    void __fastcall hookIntercept(void* loop, void*) {
        if (!interceptJoin(loop)) originalIntercept(loop);
    }

    bool replaceSlot(std::uintptr_t slot, std::uintptr_t value) {
        DWORD old{};
        auto* address = reinterpret_cast<std::uintptr_t*>(slot);
        if (!VirtualProtect(address, sizeof(value), PAGE_READWRITE, &old)) return false;
        *address = value;
        VirtualProtect(address, sizeof(value), old, &old);
        return true;
    }
}

bool installSplitScreen(HMODULE host, const Config& options) {
    if (!options.splitScreen) return true;
    base = reinterpret_cast<std::uintptr_t>(host);
    // InterceptControls walks the input driver's devices; HandleAddingRuntimePlayer starts with
    // the drop-in checks; both are the slots of the GameStandAloneLoop vtable.
    if (!codeMatches(base + interceptRva, "51 89 0C 24 8B 0D ?? ?? ?? ?? 8B 01 8B 50 60 55 33 ED FF D2 85 C0 0F 84 63 01 00 00") ||
        *reinterpret_cast<const std::uint32_t*>(base + interceptRva + 6) != base + inputGlobal ||
        !codeMatches(base + addPlayerRva, "56 57 8B 7C 24 0C 8B F1 8B 0D ?? ?? ?? ?? 57 E8 3C B2 4B 00 84 C0 75 17") ||
        !readable(reinterpret_cast<void*>(base + addPlayerSlot), 12) ||
        *reinterpret_cast<const std::uintptr_t*>(base + interceptSlot) != base + interceptRva ||
        *reinterpret_cast<const std::uintptr_t*>(base + addPlayerSlot) != base + addPlayerRva ||
        !codeMatches(base + 0xB01C30, "51 56 57 8B F9 8B 0D ?? ?? ?? ?? 8B B1 9C 00 00 00") ||
        *reinterpret_cast<const std::uint32_t*>(base + 0xB01C37) != base + playersGlobal) {
        log("SplitScreen: unavailable: player join code differs.");
        return false;
    }
    originalIntercept = reinterpret_cast<InterceptFn>(base + interceptRva);
    if (!replaceSlot(base + interceptSlot, reinterpret_cast<std::uintptr_t>(&hookIntercept))) {
        log("SplitScreen: join hook refused.");
        return false;
    }
    log("SplitScreen: START on a second controller joins as player 2 (experimental).");
    return true;
}
}
