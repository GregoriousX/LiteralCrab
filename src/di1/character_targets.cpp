// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_targets.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"

namespace crabe::di1 {
namespace {
constexpr unsigned gateRva = 0xB551B7, acceptedRva = 0xB551E0;
std::uintptr_t installedBase{}, nativeAccepted{};
void* originalGate{};
volatile LONG impactTraceCount{}, exploderTraceCount{};
constexpr unsigned tag(const char (&value)[5]) {
    return unsigned(value[0]) | (unsigned(value[1]) << 8) | (unsigned(value[2]) << 16) | (unsigned(value[3]) << 24);
}
constexpr unsigned markerTag = tag("lctg"), pistolTag = tag("pstl"), tomahawkTag = tag("toma");
constexpr unsigned allowedWeapons[] = {tag("apxb"), tag("bpxb"), tag("blas"), tag("prht"), tag("mksp"), tag("frpn"),
    tag("snow"), tag("bbgn"), tag("vprj"), tag("vpdr"), tag("chrr"), tag("chrv"),
    // These personal tools create scripted physics actors rather than entries in
    // weapons.lua: FlintLock.sx uses flnt, TB_FlamingPumpkin.sx uses pmpk.
    tag("flnt"), tag("pmpk")};
void traceUnmarkedImpact(const unsigned char* data, const unsigned char* message,
    unsigned entries, bool ignoreList) noexcept {
    const auto lastError = GetLastError();
    __try {
        const auto count = InterlockedIncrement(&exploderTraceCount);
        if (count <= 96) {
            const auto weapon = *reinterpret_cast<const unsigned*>(message + 0x58);
            bool listed{};
            for (unsigned index=0; index<entries; ++index)
                listed |= *reinterpret_cast<const unsigned*>(data + 0x28 + index*4) == weapon;
            // Native BoxBody::DoDamage writes the affected actor at +38 and
            // MunitionSystem::SendMunitionImpact writes the owning actor at +04.
            // ExploderData::MunitionImpact consumes +50 as damage and +58 as
            // FourCC. These optional observations never decide acceptance.
            const auto target = *reinterpret_cast<const unsigned*>(message + 0x38);
            const auto owner = *reinterpret_cast<const unsigned*>(message + 0x04);
            const auto damage = *reinterpret_cast<const float*>(message + 0x50);
            log("Character trace: unmarked Exploder impact data=%p target=%08X owner=%08X weapon=%08X damage=%.3g listEntries=%u listed=%d ignoreList=%d nativeListAccept=%d (native handling retained)",
                data, target, owner, weapon, static_cast<double>(damage), entries, listed, ignoreList, listed != ignoreList);
        }
        if (count == 96)
            log("Character trace: unmarked Exploder impact budget exhausted (96 events); further unmarked impact traces suppressed");
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    SetLastError(lastError);
}
bool __cdecl extendedImpact(const unsigned char* data, const unsigned char* message) noexcept {
    if (!data || !message) return false;
    __try {
        if (!(data[0xCC] & 0x80)) return false;
        const bool ignoreList = (data[0xCF] & 1) != 0;
        const bool trace=config().traceCharacterFreedom;
        if (ignoreList && !trace) return false;
        bool marked{}, pistol{}, tomahawk{};
        unsigned entries{};
        for (unsigned index = 0; index != 8; ++index) {
            const auto value = *reinterpret_cast<const unsigned*>(data + 0x28 + 4 * index);
            if (!value) break;
            ++entries;
            marked |= value == markerTag;
            pistol |= value == pistolTag;
            tomahawk |= value == tomahawkTag;
        }
        if (!marked && trace)
            traceUnmarkedImpact(data, message, entries, ignoreList);
        if (ignoreList || !marked || !pistol || !tomahawk) return false;
        const auto weapon = *reinterpret_cast<const unsigned*>(message + 0x58);
        bool extended{};
        for (const auto allowed : allowedWeapons)
            if (weapon == allowed) { extended = true; break; }
        if (config().traceCharacterFreedom) {
            const auto count = InterlockedIncrement(&impactTraceCount);
            if (count <= 96) {
                const auto lastError = GetLastError();
                log("Character trace: LR marked target impact weapon=%08X extended=%d (native handling retained)", weapon, extended);
                if (count == 96)
                    log("Character trace: LR marked target impact budget exhausted (96 events); further impact traces suppressed");
                SetLastError(lastError);
            }
        }
        return extended;
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return false;
}
// At this measured gate EDI is ExploderData, ESI the original impact message,
// and BL the native ignore-list flag. Preserve the message, every register,
// FP/SIMD state and incoming flags, then enter the native accepted/rejected path. Damage,
// attribution, impact effects, collision and scoring continue in native code.
__declspec(naked) void impactGate() {
    __asm {
        pushfd
        pushad
        mov ebx, esp
        sub esp, 528
        and esp, -16
        fxsave [esp]
        push esi
        push edi
        call extendedImpact
        add esp, 8
        fxrstor [esp]
        mov esp, ebx
        test al, al
        jz nativeGate
        popad
        popfd
        jmp dword ptr [nativeAccepted]
    nativeGate:
        popad
        popfd
        jmp dword ptr [originalGate]
    }
}
}
bool installCharacterTargets(std::uintptr_t base) {
    if (installedBase) return installedBase == base;
    // Validate both the fixed-size parser and the complete native gate, including
    // the eight-entry limit and the matched/unmatched ignore-list branches.
    if (!codeMatches(base + gateRva,
        "33 C0 8D 4F 28 8D 64 24 00 8B 11 85 D2 74 12 39 56 58 0F 84 47 01 00 00 40 83 C1 04 83 F8 08 7C E8 84 DB 0F 84 0C 04 00 00") ||
        !codeMatches(base + 0xB551AE, "8A 9F CF 00 00 00 80 E3 01") ||
        !codeMatches(base + acceptedRva, "F6 87 CC 00 00 00 40 74 12 F3 0F 10 46 50") ||
        !codeMatches(base + 0xB55316, "84 DB 0F 85 CE 02 00 00 E9 BD FE FF FF") ||
        !codeMatches(base + 0xB5DF6B, "8D 7E 28") ||
        !codeMatches(base + 0xB5DFA9, "83 C7 04 88 41 02") ||
        !codeMatches(base + 0xB5E0D0, "83 39 00 74 0B 40 83 C1 04 83 F8 08 7C F2")) return false;
    nativeAccepted = base + acceptedRva;
    auto* target = reinterpret_cast<void*>(base + gateRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&impactGate), &originalGate) != MH_OK) return false;
    if (MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target); originalGate = nullptr; nativeAccepted = 0; return false;
    }
    installedBase = base;
    impactTraceCount = 0;
    exploderTraceCount = 0;
    return true;
}
void removeCharacterTargets() noexcept {
    if (!installedBase) return;
    auto* target = reinterpret_cast<void*>(installedBase + gateRva);
    MH_DisableHook(target); MH_RemoveHook(target);
    installedBase = nativeAccepted = 0; originalGate = nullptr;
}
}
