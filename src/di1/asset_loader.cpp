// SPDX-License-Identifier: GPL-3.0-only
// DI1 profile for asset mods: Kernel::HogThread's _CheckCacheForLoad and
// _CheckCacheForLoadAndAlloc in the supported build. Both look a file up in the file thread's
// memory cache before any archive is touched; on a hit they copy the data, fill the async
// status and signal it. A mod file is served through that same path.
#include "di1/asset_loader.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace crabe::di1 {
namespace {
    constexpr std::uint32_t loadRva = 0x1E200, loadAndAllocRva = 0x1E5F0;
    constexpr std::uint32_t signalRva = 0x1D5F0, allocateRva = 0xA3B0;
    // Status fields written on completion: data pointer, byte count and error code.
    constexpr std::size_t statusData = 0x18, statusBytes = 0x14, statusError = 0x10;
    constexpr unsigned maxTraceLines = 20000;

    using LoadFn = bool(__cdecl*)(const char*, void*, unsigned, unsigned, void*);
    using LoadAndAllocFn = bool(__cdecl*)(const char*, void**, unsigned, bool, void*, void*);
    using SignalFn = void(__thiscall*)(void*);
    using AllocateFn = void*(__cdecl*)(unsigned, bool, unsigned*, void*);
    LoadFn originalLoad{};
    LoadAndAllocFn originalLoadAndAlloc{};
    SignalFn signal{};
    AllocateFn allocate{};
    AssetProvider provider{};
    bool trace{};
    std::atomic<unsigned> traced{};
    std::atomic<unsigned> served{};
    std::atomic<unsigned> characterFxTraced{};
    std::atomic<unsigned> characterToolsTraced{};
    std::atomic<unsigned> characterProjectilesTraced{};

    bool toolResource(const char* name) {
        if (!name) return false;
        // Models, scripts and DNA use different casing in original archives.
        for (auto* cursor = name; *cursor; ++cursor)
            if (!_strnicmp(cursor, "avatar_tools/", 13) || !_strnicmp(cursor, "avatar_tools\\", 13) ||
                !_strnicmp(cursor, "pnf_perry_hat", 13) || !_strnicmp(cursor, "pixar_ball", 10)) return true;
        return false;
    }


    void note(const char* kind, const char* name, bool replaced) {
        bool projectile = false;
        if (name) for (auto* cursor = name; *cursor; ++cursor)
            if (!_strnicmp(cursor, "pnf_perry_hat", 13) || !_strnicmp(cursor, "pixar_ball", 10)) { projectile = true; break; }
        // Separate budget: the general tool preload can exhaust its log allowance
        // before these projectile files arrive. Never infer missing assets from that.
        if (config().traceCharacterFreedom && projectile &&
            characterProjectilesTraced.fetch_add(1, std::memory_order_relaxed) < 128)
            log("Character trace: projectile file %s %s%s", kind, name, replaced ? " <- mod" : "");
        if (config().traceCharacterFreedom && toolResource(name) &&
            characterToolsTraced.fetch_add(1, std::memory_order_relaxed) < 256)
            log("Character trace: tool file %s %s%s", kind, name, replaced ? " <- mod" : "");
        if (config().traceCharacterFreedom && name &&
            (std::strstr(name, "choreographies/") || std::strstr(name, "Choreographies/")) &&
            characterFxTraced.fetch_add(1, std::memory_order_relaxed) < 512)
            log("Character trace: FX file %s %s%s", kind, name, replaced ? " <- mod" : "");
        if (!trace || traced.fetch_add(1, std::memory_order_relaxed) >= maxTraceLines) return;
        log("Engine %s %s%s", kind, name ? name : "<null>", replaced ? " <- mod" : "");
    }

    void prepareCompletion(void* status, void* data, unsigned bytes) {
        auto* fields = static_cast<unsigned char*>(status);
        *reinterpret_cast<void**>(fields + statusData) = data;
        *reinterpret_cast<unsigned*>(fields + statusBytes) = bytes;
        *reinterpret_cast<unsigned*>(fields + statusError) = 0;
    }

    // SEH-guarded halves: locals stay trivially destructible.
    bool serveLoad(const std::string& data, void* dest, unsigned size, unsigned offset, void* status) noexcept {
        __try {
            const std::size_t available = offset <= data.size() ? data.size() - offset : 0;
            const auto count = static_cast<unsigned>(available < size ? available : size);
            std::memcpy(dest, data.data() + (offset <= data.size() ? offset : data.size()), count);
            prepareCompletion(status, dest, count);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        // Once completion is published the waiting thread may retire the
        // request. A native signal exception must escape unchanged, never retry
        // the original loader using that completed request and filename.
        signal(status);
        return true;
    }
    bool serveAlloc(const std::string& data, void** out, unsigned extra, bool flag, void* heap, void* status) noexcept {
        __try {
            if (data.size() > std::numeric_limits<unsigned>::max() ||
                extra > std::numeric_limits<unsigned>::max() - data.size()) return false;
            const auto size = static_cast<unsigned>(data.size());
            void* buffer = allocate(size + extra, !flag, nullptr, heap);
            if (!buffer) return false;
            std::memcpy(buffer, data.data(), size);
            if (extra) std::memset(static_cast<char*>(buffer) + size, 0, extra);
            *out = buffer;
            prepareCompletion(status, buffer, size + extra);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
        signal(status);
        return true;
    }

    bool provide(const char* name, std::string& data) {
        if (!provider || !name) return false;
        try {
            return provider(name, data);
        } catch (...) {
            return false;
        }
    }

    bool __cdecl hookLoad(const char* name, void* dest, unsigned size, unsigned offset, void* status) {
        std::string data;
        if (provide(name, data)) {
            // Completion wakes another thread, which may release the request
            // and its filename immediately. Diagnostics must own their copy.
            char completedName[512]{};
            if (name) strncpy_s(completedName, name, _TRUNCATE);
            if (serveLoad(data, dest, size, offset, status)) {
                served.fetch_add(1, std::memory_order_relaxed);
                note("load", name ? completedName : nullptr, true);
                return true;
            }
        }
        note("load", name, false);
        return originalLoad(name, dest, size, offset, status);
    }
    bool __cdecl hookLoadAndAlloc(const char* name, void** out, unsigned extra, bool flag, void* heap, void* status) {
        std::string data;
        if (provide(name, data)) {
            char completedName[512]{};
            if (name) strncpy_s(completedName, name, _TRUNCATE);
            if (serveAlloc(data, out, extra, flag, heap, status)) {
                served.fetch_add(1, std::memory_order_relaxed);
                note("load+alloc", name ? completedName : nullptr, true);
                return true;
            }
        }
        note("load+alloc", name, false);
        return originalLoadAndAlloc(name, out, extra, flag, heap, status);
    }
}

bool installAssetLoader(HMODULE host, AssetProvider assets, bool traceRequests) {
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!codeMatches(base + loadRva, "8B 44 24 04 81 EC 84 00 00 00 57 50 8D 4C 24 0C 68 80 00 00 00 51 E8 85 F5 FF FF") ||
        !codeMatches(base + loadAndAllocRva, "8B 44 24 04 81 EC 84 00 00 00 56 50 8D 4C 24 0C 68 80 00 00 00 51 E8 95 F1 FF FF") ||
        !codeMatches(base + signalRva, "8B 01 50 FF 15") || !codeMatches(base + allocateRva, "8B 44 24 0C 8B 4C 24 04 85 C0 74 02 89 08") ||
        !callsTo(base, 0x1E28F, signalRva) || !callsTo(base, 0x1E69C, signalRva) || !callsTo(base, 0x1E662, allocateRva) ||
        !callsTo(base, 0x1EC86, loadAndAllocRva) || !callsTo(base, 0x1EBD0, loadRva)) {
        log("Mods: file loader signature differs; files inside archives cannot be replaced.");
        return false;
    }
    provider = assets;
    trace = traceRequests;
    signal = reinterpret_cast<SignalFn>(base + signalRva);
    allocate = reinterpret_cast<AllocateFn>(base + allocateRva);
    void* targets[] = {reinterpret_cast<void*>(base + loadRva), reinterpret_cast<void*>(base + loadAndAllocRva)};
    const bool loadCreated = MH_CreateHook(targets[0], reinterpret_cast<void*>(&hookLoad), reinterpret_cast<void**>(&originalLoad)) == MH_OK;
    const bool allocCreated = loadCreated &&
        MH_CreateHook(targets[1], reinterpret_cast<void*>(&hookLoadAndAlloc), reinterpret_cast<void**>(&originalLoadAndAlloc)) == MH_OK;
    const bool ok = loadCreated && allocCreated && MH_EnableHook(targets[0]) == MH_OK && MH_EnableHook(targets[1]) == MH_OK;
    if (!ok) {
        // MinHook can refuse a hook that another module created but has not yet
        // enabled. Its native bytes still pass the profile. Roll back only ours.
        if (allocCreated) { MH_DisableHook(targets[1]); MH_RemoveHook(targets[1]); }
        if (loadCreated) { MH_DisableHook(targets[0]); MH_RemoveHook(targets[0]); }
        log("Mods: file loader hook refused.");
        return false;
    }
    log("Mods: file loader hooked (RVA 0x%X/0x%X)%s.", loadRva, loadAndAllocRva, trace ? "; requests are traced" : "");
    return true;
}
}
