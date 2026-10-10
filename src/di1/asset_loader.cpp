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

    bool matches(std::uintptr_t address, const char* hex) {
        for (std::size_t offset = 0;; ++offset) {
            while (*hex == ' ') ++hex;
            if (!*hex) return true;
            char* next{};
            const auto byte = std::strtoul(hex, &next, 16);
            if (next == hex || !readable(reinterpret_cast<const void*>(address + offset), 1) ||
                *reinterpret_cast<const unsigned char*>(address + offset) != byte) return false;
            hex = next;
        }
    }
    bool calls(std::uintptr_t base, std::uint32_t site, std::uint32_t target) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(base + site);
        return readable(bytes, 5) && bytes[0] == 0xE8 &&
            base + site + 5 + *reinterpret_cast<const std::int32_t*>(bytes + 1) == base + target;
    }

    void note(const char* kind, const char* name, bool replaced) {
        if (!trace || traced.fetch_add(1, std::memory_order_relaxed) >= maxTraceLines) return;
        log("Engine %s %s%s", kind, name ? name : "<null>", replaced ? " <- mod" : "");
    }

    void complete(void* status, void* data, unsigned bytes) {
        auto* fields = static_cast<unsigned char*>(status);
        *reinterpret_cast<void**>(fields + statusData) = data;
        *reinterpret_cast<unsigned*>(fields + statusBytes) = bytes;
        *reinterpret_cast<unsigned*>(fields + statusError) = 0;
        signal(status);
    }

    // SEH-guarded halves: locals stay trivially destructible.
    bool serveLoad(const std::string& data, void* dest, unsigned size, unsigned offset, void* status) noexcept {
        __try {
            const std::size_t available = offset <= data.size() ? data.size() - offset : 0;
            const auto count = static_cast<unsigned>(available < size ? available : size);
            std::memcpy(dest, data.data() + (offset <= data.size() ? offset : data.size()), count);
            complete(status, dest, count);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
    bool serveAlloc(const std::string& data, void** out, unsigned extra, bool flag, void* heap, void* status) noexcept {
        __try {
            const auto size = static_cast<unsigned>(data.size());
            void* buffer = allocate(size + extra, !flag, nullptr, heap);
            if (!buffer) return false;
            std::memcpy(buffer, data.data(), size);
            if (extra) std::memset(static_cast<char*>(buffer) + size, 0, extra);
            *out = buffer;
            complete(status, buffer, size + extra);
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
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
        if (provide(name, data) && serveLoad(data, dest, size, offset, status)) {
            served.fetch_add(1, std::memory_order_relaxed);
            note("load", name, true);
            return true;
        }
        note("load", name, false);
        return originalLoad(name, dest, size, offset, status);
    }
    bool __cdecl hookLoadAndAlloc(const char* name, void** out, unsigned extra, bool flag, void* heap, void* status) {
        std::string data;
        if (provide(name, data) && serveAlloc(data, out, extra, flag, heap, status)) {
            served.fetch_add(1, std::memory_order_relaxed);
            note("load+alloc", name, true);
            return true;
        }
        note("load+alloc", name, false);
        return originalLoadAndAlloc(name, out, extra, flag, heap, status);
    }
}

bool installAssetLoader(HMODULE host, AssetProvider assets, bool traceRequests) {
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!matches(base + loadRva, "8B 44 24 04 81 EC 84 00 00 00 57 50 8D 4C 24 0C 68 80 00 00 00 51 E8 85 F5 FF FF") ||
        !matches(base + loadAndAllocRva, "8B 44 24 04 81 EC 84 00 00 00 56 50 8D 4C 24 0C 68 80 00 00 00 51 E8 95 F1 FF FF") ||
        !matches(base + signalRva, "8B 01 50 FF 15") || !matches(base + allocateRva, "8B 44 24 0C 8B 4C 24 04 85 C0 74 02 89 08") ||
        !calls(base, 0x1E28F, signalRva) || !calls(base, 0x1E69C, signalRva) || !calls(base, 0x1E662, allocateRva) ||
        !calls(base, 0x1EC86, loadAndAllocRva) || !calls(base, 0x1EBD0, loadRva)) {
        log("Mods: file loader signature differs; files inside archives cannot be replaced.");
        return false;
    }
    provider = assets;
    trace = traceRequests;
    signal = reinterpret_cast<SignalFn>(base + signalRva);
    allocate = reinterpret_cast<AllocateFn>(base + allocateRva);
    void* targets[] = {reinterpret_cast<void*>(base + loadRva), reinterpret_cast<void*>(base + loadAndAllocRva)};
    bool ok = MH_CreateHook(targets[0], reinterpret_cast<void*>(&hookLoad), reinterpret_cast<void**>(&originalLoad)) == MH_OK &&
        MH_CreateHook(targets[1], reinterpret_cast<void*>(&hookLoadAndAlloc), reinterpret_cast<void**>(&originalLoadAndAlloc)) == MH_OK &&
        MH_EnableHook(targets[0]) == MH_OK && MH_EnableHook(targets[1]) == MH_OK;
    if (!ok) {
        for (const auto target : targets) { MH_DisableHook(target); MH_RemoveHook(target); }
        log("Mods: file loader hook refused.");
        return false;
    }
    log("Mods: file loader hooked (RVA 0x%X/0x%X)%s.", loadRva, loadAndAllocRva, trace ? "; requests are traced" : "");
    return true;
}
}
