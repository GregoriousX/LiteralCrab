// SPDX-License-Identifier: GPL-3.0-only
// [PostProcessing]: switch off motion blur and depth of field.
//
// DI1 does not decide these per frame in code. Each realm, world and Toy Box sky carries scene
// settings (SSET chunks in its .mtb material bundle), and the post-processing shaders read their
// strengths from the blended settings. The game itself turns motion blur off this way: its
// "SkipMotionBlur" realm and the Cars race worlds set the motion blur value to 0, while
// "DOF_50m" and several Toy Box skies raise the depth of field value above its usual 0.
//
// An SSET chunk is a 16-byte header with the block count, the blocks' GUID names, then the
// blocks themselves from the next 16-byte boundary, each 0x9F0 bytes in the format of
// Shaders/SceneSettingsFormat.ssf. The two values were identified by comparing those realms and
// checked over all 190 chunks the game ships. The option writes 0 into every block just before
// the game's own loader reads the chunk, so the game blends and applies the result as usual.
#include "di1/post_effects.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cstdint>
#include <cstring>

namespace crabe::di1 {
namespace {
    // The material bundle's SSET chunk loader, its caller's chunk-type check, and the loader's
    // block stride calculation: aligned start after the names, (size - start) / count.
    constexpr std::uint32_t loaderRva = 0xB78E0, typeCheckRva = 0xC6436, strideRva = 0xB79BF;
    constexpr std::size_t blockSize = 0x9F0, motionBlurOffset = 0x920, depthOfFieldOffset = 0x3CC;
    constexpr std::uint32_t maxBlocks = 64;

    using LoaderFn = void(__thiscall*)(void*, void*);
    LoaderFn original{};
    bool motionBlurOff{}, depthOfFieldOff{};
    std::atomic<unsigned> patched{}, skipped{};

    // Returns false for a chunk whose layout is not the measured one; it is then left alone.
    bool neutralize(unsigned char* chunk) noexcept {
        __try {
            if (std::memcmp(chunk, "SSET", 4)) return false;
            std::uint32_t size, count;
            std::memcpy(&size, chunk + 8, 4);
            std::memcpy(&count, chunk + 12, 4);
            if (!count || count > maxBlocks || size > 0x100000) return false;
            std::size_t cursor = 0x14;
            for (std::uint32_t name = 0; name < count; ++name) {
                while (cursor < size && chunk[cursor]) ++cursor;
                if (++cursor > size) return false;
            }
            const std::size_t start = (cursor + 15) & ~static_cast<std::size_t>(15);
            if (start > size || size - start != count * blockSize) return false;
            for (std::uint32_t block = 0; block < count; ++block) {
                unsigned char* values = chunk + start + block * blockSize;
                const float zero = 0.0f;
                if (motionBlurOff) std::memcpy(values + motionBlurOffset, &zero, 4);
                if (depthOfFieldOff) std::memcpy(values + depthOfFieldOffset, &zero, 4);
            }
            return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    void __fastcall hookLoader(void* bundle, void*, void* chunk) {
        if (chunk && neutralize(static_cast<unsigned char*>(chunk))) patched.fetch_add(1, std::memory_order_relaxed);
        else if (skipped.fetch_add(1, std::memory_order_relaxed) == 0) log("PostProcessing: a scene settings chunk has another layout; left unchanged.");
        original(bundle, chunk);
    }
}

bool installPostEffectHooks(HMODULE host, const Config& options) {
    motionBlurOff = !options.motionBlur;
    depthOfFieldOff = !options.depthOfField;
    if (!motionBlurOff && !depthOfFieldOff) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!codeMatches(base + loaderRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 28 A1 ?? ?? ?? ?? 53 55 56") ||
        !codeMatches(base + strideRva, "8B 74 24 48 8B 47 08 83 C6 0F 83 E6 F0 2B C6 03 C7 33 D2 F7 F3") ||
        !codeMatches(base + typeCheckRva, "3D 53 53 45 54 75 1D 56 8B CF E8") || !callsTo(base, typeCheckRva + 10, loaderRva)) {
        log("PostProcessing: scene settings loader differs; effects stay as the game sets them.");
        return false;
    }
    auto* target = reinterpret_cast<void*>(base + loaderRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&hookLoader), reinterpret_cast<void**>(&original)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        log("PostProcessing: hook refused.");
        return false;
    }
    log("PostProcessing: motion blur %s, depth of field %s.", motionBlurOff ? "off" : "on", depthOfFieldOff ? "off" : "on");
    return true;
}
}
