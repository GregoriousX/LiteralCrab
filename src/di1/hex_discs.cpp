// SPDX-License-Identifier: GPL-3.0-only
#include "di1/hex_discs.hpp"
#include <cstdint>
#include <cstring>

namespace crabe::di1 {
namespace {
// In IGPData::AddCoin, after a disc kind other than AvatarModifier (round discs): the Toy Box test
// (IGPGameFlow::IsRumpusRoom, 0xB769A0) jumps on to the disc's kind; a failed test falls through to
// the "only in the Toy Box" popup. Outside the Toy Box the jump also takes an Object (spawned by
// IGPGameFlow::SpawnObject) and an Item (IGPGameFlow::Award). Skies and terrains change the Toy Box
// realm through its authoring system, which a play set lacks, so they keep the popup.
constexpr unsigned gateRva = 0x789FD8, toyBoxRva = 0x7769A0, kindPathRva = 0x78A072, popupRva = 0x789FDE;
constexpr unsigned char original[] = {0x0F, 0x85, 0x94, 0x00, 0x00, 0x00};
std::uintptr_t installedBase{}, kindPath{}, popup{};

bool __cdecl spawnableKind(const char* kind) noexcept {
    __try { return kind && (!_stricmp(kind, "Object") || !_stricmp(kind, "Item")); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// At the gate AL holds the Toy Box test and [esp+44] the disc kind (the native AvatarModifier
// comparison reads it there). Registers and flags are preserved for either native path.
__declspec(naked) void kindGate() {
    __asm {
        test al, al
        jnz toyBox
        pushfd
        pushad
        mov eax, [esp + 0x44 + 0x24]
        push eax
        call spawnableKind
        add esp, 4
        test al, al
        popad
        jnz allowed
        popfd
        jmp dword ptr [popup]
    allowed:
        popfd
    toyBox:
        jmp dword ptr [kindPath]
    }
}

bool write(std::uintptr_t address, const unsigned char* bytes, std::size_t length) {
    DWORD old{};
    if (!VirtualProtect(reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(reinterpret_cast<void*>(address), bytes, length);
    VirtualProtect(reinterpret_cast<void*>(address), length, old, &old);
    FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), length);
    return true;
}
}

bool installHexDiscs(HMODULE host, const Config& options) {
    if (!options.hexDiscsInPlaysets) return true;
    if (installedBase) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    // The AvatarModifier comparison, the Toy Box test and its jump, the popup and the Object branch.
    if (!codeMatches(base + 0x789FB5, "68 ?? ?? ?? ?? 52 E8 ?? ?? ?? ?? 83 C4 08 85 C0 0F 84 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? E8") ||
        !callsTo(base, 0x789FD1, toyBoxRva) || !codeMatches(base + 0x789FD6, "84 C0 0F 85 94 00 00 00") ||
        !codeMatches(base + toyBoxRva, "6A 00 6A 00 6A 00 6A 00 E8 ?? ?? ?? ?? 84 C0 0F 95 C0 C3") ||
        !codeMatches(base + 0x78A001, "68") || !codeMatches(base + 0x78A076, "68") ||
        std::strcmp(*reinterpret_cast<const char* const*>(base + 0x78A002), "IGP_HexCoinOnlyToybox") ||
        std::strcmp(*reinterpret_cast<const char* const*>(base + 0x78A077), "Object") ||
        std::strcmp(*reinterpret_cast<const char* const*>(base + 0x789FB6), "AvatarModifier")) {
        log("Gameplay: HexDiscsInPlaysets unavailable: portal disc handler differs; nothing changed.");
        return false;
    }
    kindPath = base + kindPathRva; popup = base + popupRva;
    unsigned char patched[6] = {0xE9, 0, 0, 0, 0, 0x90};
    const auto relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&kindGate) - (base + gateRva + 5));
    std::memcpy(patched + 1, &relative, sizeof(relative));
    if (!write(base + gateRva, patched, sizeof(patched))) {
        log("Gameplay: HexDiscsInPlaysets refused: code page not writable.");
        return false;
    }
    installedBase = base;
    log("Gameplay: hexagonal power disc vehicles, mounts and tools from the virtual portal work outside the Toy Box.");
    return true;
}
void removeHexDiscs() noexcept {
    if (!installedBase) return;
    write(installedBase + gateRva, original, sizeof(original));
    installedBase = 0;
}
}
