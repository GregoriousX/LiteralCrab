// SPDX-License-Identifier: GPL-3.0-only
// [Camera] ReturnDelay: how long the camera waits after the player turns it before it returns
// behind the character.
//
// Every DI1 camera describes that return with CameraReturn values (YawReturn, PitchReturn,
// DistanceReturn), held in TypeProperty<CameraReturn> objects whether they come from camera
// assets, graph bindings or Lua. Cameras read them through the property's Get, which copies the
// current value out. The option replaces Delay in that copy only, so the stored camera data
// stays untouched and "default" restores the original behavior.
#include "di1/camera.hpp"
#include "minhook/MinHook.h"
#include <cstdint>
#include <cstring>

namespace crabe::di1 {
namespace {
    // TypeProperty<CameraReturn>::Get and its vtable slot; the Lua reader that stores Delay.
    constexpr std::uint32_t getRva = 0xC6E150, getSlot = 0x1076038, delayReaderRva = 0xC63A7C;
    constexpr std::size_t delayOffset = 0x20;

    using GetFn = void*(__thiscall*)(void*, void*, void*);
    GetFn original{};
    float delay{};

    void* __fastcall hookGet(void* property, void*, void* out, void* context) {
        void* value = original(property, out, context);
        if (value) *reinterpret_cast<float*>(static_cast<unsigned char*>(value) + delayOffset) = delay;
        return value;
    }
}

bool installCameraHooks(HMODULE host, const Config& options) {
    if (options.cameraReturnDelay < 0.0f) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    // Get copies the 0x38-byte value out (after evaluating a binding, if any) and returns it.
    // The reader confirms the field: Delay is parsed into offset 0x20.
    const auto* name = codeMatches(base + delayReaderRva, "68 ?? ?? ?? ?? 57 E8 09 52 A3 FF D9 5E 20")
        ? *reinterpret_cast<const char* const*>(base + delayReaderRva + 1) : nullptr;
    if (!codeMatches(base + getRva, "83 EC 38 53 8B 5C 24 44 55 56 57 8B E9 85 DB 74 57 83 7D 48 00 75 06 83 7D 4C 00 74 3D "
            "8B 45 4C 8B 4D 48 8D 54 24 50 52 53 8D 54 24 18 52 C6 44 24 5C 00 FF D0 80 7D 0D 00 8D 7D 10 B9 0E 00 00 00 8B F0 F3 A5") ||
        !codeMatches(base + getRva + 0x68, "8B 44 24 4C 8D 75 10 B9 0E 00 00 00 8B F8 F3 A5") ||
        !readable(reinterpret_cast<void*>(base + getSlot), 4) ||
        *reinterpret_cast<const std::uintptr_t*>(base + getSlot) != base + getRva ||
        !name || !readable(name, 6) || std::memcmp(name, "Delay", 6)) {
        log("Camera: ReturnDelay unavailable: camera code differs.");
        return false;
    }
    delay = options.cameraReturnDelay;
    auto* target = reinterpret_cast<void*>(base + getRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&hookGet), reinterpret_cast<void**>(&original)) != MH_OK ||
        MH_EnableHook(target) != MH_OK) {
        MH_RemoveHook(target);
        log("Camera: ReturnDelay hook refused.");
        return false;
    }
    log("Camera: return delay %.2f s for every camera (game values ignored).", delay);
    return true;
}
}
