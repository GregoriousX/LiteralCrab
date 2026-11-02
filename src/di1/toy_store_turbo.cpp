// SPDX-License-Identifier: GPL-3.0-only
#include "di1/toy_store_turbo.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"

namespace crabe::di1 {
namespace {
// CarEnergy (Wii U __ct__9CarEnergyFR6CActor): +0x60 the vehicle's handle, +0x64 its ToyBoxVehicle
// component (driver handle at +0xE4), +0xF6 "no turbo locks" (!UseCarsTurboLocks). Only SetEnergy
// (the meter's maximum: MaxCarEnergy, or 20/30/40 by UP_TurboPurchase/UP_TurboUpgrade_1/_2) and
// CreateEnergyMeter (under locks only for a player's avatar) read +0xF6.
constexpr unsigned setEnergyRva = 0x7D6420, createMeterRva = 0x7D6680, playerFromAvatarRva = 0xAE8070,
    playersRva = 0x135766C;
using SetEnergy = void(__thiscall*)(void*, float, bool);
using CreateMeter = void(__thiscall*)(void*);
using PlayerFromAvatar = int(__thiscall*)(void*, std::uint32_t);
std::uintptr_t installedBase{};
SetEnergy originalSetEnergy{}; CreateMeter originalCreateMeter{};
PlayerFromAvatar playerFromAvatar{};
void** players{};
unsigned reports{};

// True for a driven vehicle that is not itself a player's avatar while the zone uses the Cars locks.
bool toyBoxRules(unsigned char* energy) noexcept {
    __try {
        if (energy[0xF6]) return false;
        const auto vehicle = *reinterpret_cast<const std::uint32_t*>(energy + 0x60);
        const auto* toyBox = *reinterpret_cast<const unsigned char* const*>(energy + 0x64);
        const auto driver = toyBox ? *reinterpret_cast<const std::uint32_t*>(toyBox + 0xE4) : 0;
        if (!vehicle || !driver || driver == vehicle || !*players) return false;
        return playerFromAvatar(*players, vehicle) < 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// The override lasts for the one native call; the locks stay recorded for the vehicle's next driver.
template <class Call> void withToyBoxRules(void* self, Call call) {
    auto* energy = static_cast<unsigned char*>(self);
    if (!toyBoxRules(energy)) { call(); return; }
    energy[0xF6] = 1;
    call();
    energy[0xF6] = 0;
}
void __fastcall setEnergy(void* self, void*, float value, bool notify) {
    withToyBoxRules(self, [&] { originalSetEnergy(self, value, notify); });
}
void __fastcall createMeter(void* self, void*) {
    withToyBoxRules(self, [&] {
        if (reports < 8) {
            ++reports;
            log("ToyStore: turbo meter for vehicle %08X driven by a character in a Cars-locked zone (Toy Box rules).",
                *reinterpret_cast<const std::uint32_t*>(static_cast<unsigned char*>(self) + 0x60));
        }
        // The meter's segments follow the maximum: refresh it under the Toy Box rules first.
        originalSetEnergy(self, *static_cast<const float*>(self), false);
        originalCreateMeter(self);
    });
}
bool hook(std::uintptr_t base, unsigned rva, void* detour, void** original) noexcept {
    auto* target = reinterpret_cast<void*>(base + rva);
    if (MH_CreateHook(target, detour, original) != MH_OK) return false;
    if (MH_EnableHook(target) == MH_OK) return true;
    MH_RemoveHook(target); *original = nullptr; return false;
}
}

bool installToyStoreTurbo(std::uintptr_t base) {
    if (installedBase) return installedBase == base;
    if (!codeMatches(base + setEnergyRva, "51 56 8B F1 80 BE F6 00 00 00 00 F3 0F 10 06") ||
        !codeMatches(base + setEnergyRva + 0x14A, "5E 59 C2 08 00") ||
        !codeMatches(base + createMeterRva, "51 53 55 56 57 8B F9 8B 47 60 50 E8 ?? ?? ?? ?? 83 C4 04 80 BF F6 00 00 00 00") ||
        !codeMatches(base + 0x7D66CF, "8B 0D ?? ?? ?? ?? 50 E8") || !callsTo(base, 0x7D66D6, playerFromAvatarRva) ||
        *reinterpret_cast<const std::uint32_t*>(base + 0x7D66D1) != base + playersRva ||
        !codeMatches(base + playerFromAvatarRva, "83 7C 24 04 00 53 8B D9 75 07 83 C8 FF 5B C2 04 00") ||
        // SetShowEnergyMeterUI: a driver at ToyBoxVehicle +0xE4, then the meter is created on demand.
        !codeMatches(base + 0x7E1F31, "8B 56 64 83 BA E4 00 00 00 00") ||
        !codeMatches(base + 0x7E1F4D, "83 7E 30 00 75 07 8B CE") || !callsTo(base, 0x7E1F55, createMeterRva) ||
        // The constructor stores !UseCarsTurboLocks at +0xF6.
        !callsTo(base, 0x7EED51, 0x7CA110) || !codeMatches(base + 0x7EED5D, "88 8E F6 00 00 00")) return false;
    players = reinterpret_cast<void**>(base + playersRva);
    playerFromAvatar = reinterpret_cast<PlayerFromAvatar>(base + playerFromAvatarRva);
    reports = 0;
    if (!hook(base, setEnergyRva, reinterpret_cast<void*>(&setEnergy), reinterpret_cast<void**>(&originalSetEnergy)))
        return false;
    installedBase = base;
    if (!hook(base, createMeterRva, reinterpret_cast<void*>(&createMeter), reinterpret_cast<void**>(&originalCreateMeter))) {
        removeToyStoreTurbo(); return false;
    }
    return true;
}
void removeToyStoreTurbo() noexcept {
    if (!installedBase) return;
    for (const auto rva : {setEnergyRva, createMeterRva}) {
        auto* target = reinterpret_cast<void*>(installedBase + rva);
        MH_DisableHook(target); MH_RemoveHook(target);
    }
    installedBase = 0; originalSetEnergy = nullptr; originalCreateMeter = nullptr;
}
}
