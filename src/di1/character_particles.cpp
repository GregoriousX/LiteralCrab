// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_particles.hpp"
#include "di1/particle_directory.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <intrin.h>

namespace crabe::di1 {
namespace {
constexpr unsigned loadRva = 0x2EF6F0, importRva = 0x2EF350, unloadRva = 0x2CD910;
using Load = void*(__cdecl*)(const char*, unsigned);
using Import = bool(__cdecl*)(void*, unsigned, void*);
using Unload = void(__cdecl*)(void*);
using Find = void*(__thiscall*)(void*, unsigned);
Load originalLoad{}; Import originalImport{}; Unload originalUnload{}; Find find{};
std::uintptr_t imageBase{};
void** fxSystem{};
const unsigned short* envelopeCount{};
bool diagnostics{}, loadInstalled{}, importInstalled{}, unloadInstalled{};
void* worldLibrary{}; void* supplementLibrary{};
bool attempted{};
thread_local bool importingSupplement{};
bool importedSupplement{};
unsigned events{};
constexpr unsigned maps[] = {0x10, 0x4C, 0x88};

void* manager() noexcept {
    return fxSystem && *fxSystem ? static_cast<unsigned char*>(*fxSystem) + 0x18 : nullptr;
}
void* record(unsigned kind, unsigned id) {
    auto* resources = static_cast<unsigned char*>(manager());
    return resources ? find(resources + maps[kind], id) : nullptr;
}
bool __cdecl importParticles(void* data, unsigned size, void* materials) {
    if (!importingSupplement) return originalImport(data, size, materials);
    ParticleDirectory view;
    if (!manager() || !view.parse(data, size) || !envelopeCount || !materials) {
        log("Character freedom: supplemental particle import refused (format/envelope budget).");
        return false;
    }
    const auto materialCount = *reinterpret_cast<const unsigned*>(static_cast<const unsigned char*>(materials) + 0x10);
    if (!filterParticleDirectory(data, size, [&view, materialCount](unsigned kind, unsigned id, unsigned) {
            // Native import does not register particles lacking a valid material.
            // Exclude them before collecting curve references to avoid orphan curves.
            if (kind == 0) {
                for (unsigned i = 0; i < view.count[0]; ++i) {
                    const auto* row = view.row(0, i);
                    if (view.read<unsigned>(row + 4) != id) continue;
                    const auto material = view.read<std::uint16_t>(row + 14);
                    if (material == 0xFFFF || material >= materialCount) return false;
                    break;
                }
            }
            return record(kind, id) == nullptr;
        })) {
        log("Character freedom: supplemental particle import refused (directory filter).");
        return false;
    }
    if (!compactParticleEnvelopes(data, size) || !view.parse(data, size)) {
        log("Character freedom: supplemental particle import refused (curve references).");
        return false;
    }
    if (unsigned(*envelopeCount) + view.curves >= 20000) {
        log("Character freedom: supplemental particle import refused (curve budget: current=%u added=%u).",
            unsigned(*envelopeCount), unsigned(view.curves));
        return false;
    }
    const bool result = originalImport(data, size, materials);
    importedSupplement = result;
    if (diagnostics && events++ < 24) {
        view.parse(data, size);
        log("Character freedom: supplemental particles imported=%d particles=%u effects=%u emitters=%u curves=%u",
            result, view.count[0], view.count[1], view.count[2], view.curves);
    }
    return result;
}
bool worldParticleFile(const char* name) noexcept {
    if (!name) return false;
    for (const auto* world : {"particles/ca_radiatorsprings", "particles/in_city", "particles/in_city_combat",
        "particles/lr_colby", "particles/mu_master", "particles/pc_oceanmaster", "particles/ts_master"})
        if (!_stricmp(name, world)) return true;
    return false;
}
void preloadWorldParticles() {
    if (attempted || supplementLibrary || !manager()) return;
    attempted = true; importedSupplement = false;
    importingSupplement = true;
    void* library = originalLoad("particles/rumpusroom", 0);
    importingSupplement = false;
    // Cached libraries belong to their native owner. Never queue their release.
    if (library && importedSupplement) supplementLibrary = library;
    if (diagnostics && events++ < 24)
        log("Character freedom: supplemental Toy Box particle preload available=%d owned=%d thread=%u",
            library != nullptr, supplementLibrary != nullptr, GetCurrentThreadId());
}
void* __cdecl loadParticles(const char* name, unsigned flags) {
    const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
    void* result = originalLoad(name, flags);
    // Observe the level's own native library, not arbitrary tool/resource loads.
    if (caller == imageBase + 0xB1B575 && result && !flags && !worldLibrary && worldParticleFile(name)) {
        worldLibrary = result; attempted = false;
        if (diagnostics && events++ < 24) log("Character freedom: playset particle library tracked name=%s", name);
        // Load while still in the verified native level-load path. Emitter
        // initialization may run on another thread or before PlaysetProperties
        // is ready; it must never synchronously load a library in that context.
        preloadWorldParticles();
    }
    return result;
}
void __cdecl unloadParticles(void* library) {
    if (library == supplementLibrary) {
        supplementLibrary = nullptr;
    } else if (library == worldLibrary) {
        worldLibrary = nullptr; attempted = true;
        if (supplementLibrary) {
            void* owned = supplementLibrary; supplementLibrary = nullptr;
            // Queue in the same native deferred-release phase as the world.
            // Do not free recipe/material memory while live effects still use it.
            originalUnload(owned);
            if (diagnostics && events++ < 24) log("Character freedom: supplemental particle library queued for native release.");
        }
    }
    originalUnload(library);
}
bool createHook(unsigned rva, void* callback, void*& trampoline, bool& created) {
    void* target = reinterpret_cast<void*>(imageBase + rva);
    created = MH_CreateHook(target, callback, &trampoline) == MH_OK;
    return created;
}
}

bool installCharacterParticles(std::uintptr_t base, bool trace) {
    if (!codeMatches(base + loadRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00") ||
        !codeMatches(base + importRva, "8B 44 24 08 83 EC 0C 55 56 8B 74 24 18 50 56 E8") ||
        !callsTo(base, 0x2EF35F, 0x2C81E0) || !callsTo(base, 0x2EF53C, importRva) ||
        !codeMatches(base + unloadRva, "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 A1") ||
        !callsTo(base, 0xB086BF, 0x2CD9C0) || !callsTo(base, 0xB1B570, loadRva) ||
        !codeMatches(base + 0x2CD9CC, "E9 3F FF FF FF") ||
        !codeMatches(base + 0x2EF3BB, "8B 0D ?? ?? ?? ?? 83 C4 0C 50 56 83 C1 18 E8") ||
        !callsTo(base, 0x2D8A88, 0xB75DF0) || !callsTo(base, 0x2D8AA8, 0xB75DF0) ||
        !callsTo(base, 0x2D8ACB, 0xB75DF0) ||
        !codeMatches(base + 0x2C7166, "0F B7 35 ?? ?? ?? ?? 57 8D 3C 30 81 FF 20 4E 00 00") ||
        !callsTo(base, 0x2EF3B2, 0x2C8210) || !codeMatches(base + 0x2EF3AF, "6A 06 56 E8") ||
        !codeMatches(base + 0x2EF453, "0F B6 86 05 01 00 00 52 50 8D 8E 08 01 00 00 51 E8") ||
        !callsTo(base, 0x2EF463, 0x2C8210) ||
        !callsTo(base, 0x2E6E84, 0x2E6DD0) || !callsTo(base, 0x2E6E0B, 0x2E5C70) ||
        !callsTo(base, 0x2E6E1A, 0x2E40C0) || !callsTo(base, 0x2E6E29, 0x2E5D70) ||
        !codeMatches(base + 0x2E5CBA, "8B 46 0C 39 44 24 3C 77 7D 3B 44 24 40 73 77") ||
        !codeMatches(base + 0x2E40E0, "8B 46 0C 39 44 24 14 77 10 3B 44 24 18 73 0A") ||
        !codeMatches(base + 0x2E5DC1, "8B 46 0C 39 44 24 40 0F 87 8A 00 00 00 3B 44 24 44")) return false;
    imageBase = base; diagnostics = trace;
    fxSystem = *reinterpret_cast<void***>(base + 0x2EF3BD);
    envelopeCount = *reinterpret_cast<const unsigned short**>(base + 0x2C7169);
    if (!readable(fxSystem, sizeof(void*)) || !readable(envelopeCount, sizeof(*envelopeCount))) {
        imageBase = 0; return false;
    }
    find = reinterpret_cast<Find>(base + 0xB75DF0);
    void* trampoline{};
    if (!createHook(loadRva, reinterpret_cast<void*>(&loadParticles), trampoline, loadInstalled)) {
        removeCharacterParticles(); return false;
    }
    originalLoad = reinterpret_cast<Load>(trampoline);
    if (!createHook(importRva, reinterpret_cast<void*>(&importParticles), trampoline, importInstalled)) {
        removeCharacterParticles(); return false;
    }
    originalImport = reinterpret_cast<Import>(trampoline);
    if (!createHook(unloadRva, reinterpret_cast<void*>(&unloadParticles), trampoline, unloadInstalled)) {
        removeCharacterParticles(); return false;
    }
    originalUnload = reinterpret_cast<Unload>(trampoline);
    attempted = importedSupplement = false; events = 0;
    // Publish every original and initialize state before any callback can run.
    // Enable the load path last: it may synchronously enter import/unload.
    for (const auto rva : {importRva, unloadRva, loadRva}) {
        if (MH_EnableHook(reinterpret_cast<void*>(base + rva)) != MH_OK) {
            removeCharacterParticles(); return false;
        }
    }
    return true;
}
void removeCharacterParticles() noexcept {
    if (!imageBase) return;
    if (supplementLibrary && originalUnload) {
        originalUnload(supplementLibrary);
    }
    worldLibrary = supplementLibrary = nullptr;
    for (auto [rva, installed] : {std::pair{unloadRva, &unloadInstalled}, std::pair{importRva, &importInstalled}, std::pair{loadRva, &loadInstalled}}) {
        if (!*installed) continue;
        auto* target = reinterpret_cast<void*>(imageBase + rva);
        MH_DisableHook(target); MH_RemoveHook(target); *installed = false;
    }
    imageBase = 0;
}
}
