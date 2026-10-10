// SPDX-License-Identifier: GPL-3.0-only
// Port of upstream EngineResolution, measured against the supported DI1 PC build.
#include "di1/engine.hpp"
#include "di1/common.hpp"
#include "di1/display.hpp"
#include "minhook/MinHook.h"
#include <intrin.h>
#include <cstdint>
#include <cstdlib>

namespace crabe::di1 {
namespace {
    constexpr std::uint32_t supportedRva = 0x98EC0;
    constexpr std::uint32_t containsRva = 0x24C60;
    constexpr std::uint32_t viewportRva = 0x3C5440;
    constexpr std::uint32_t targetRva = 0x9D890;
    constexpr std::uint32_t fovRva = 0xA59C0;
    constexpr std::uint32_t aspectRva = 0xA5A10;
    constexpr std::uint32_t applyProjectionRva = 0xD7FA70;
    constexpr std::uint32_t keyboardFlagsRva = 0x98369;
    std::uintptr_t base{};
    void* supportedOriginal{};
    void* containsOriginal{};
    void* viewportOriginal{};
    void* fovOriginal{};
    void* aspectOriginal{};
    bool keyboardPatched{};
    void* (__cdecl* defaultTarget)(){};
    // Diagnostics only: WindowsSystemInputDriver global and its vtable in the supported build.
    constexpr std::uint32_t inputDriverRva = 0x121D060;
    constexpr std::uint32_t inputDriverVtableRva = 0xE9BAD4;
    void logInputState(bool active) {
        const auto slot = reinterpret_cast<const std::uintptr_t*>(base + inputDriverRva);
        const auto driver = readable(slot, sizeof(*slot)) ? reinterpret_cast<const BYTE*>(*slot) : nullptr;
        if (!driver || !readable(driver, 0x228) ||
            *reinterpret_cast<const std::uintptr_t*>(driver) != base + inputDriverVtableRva) {
            log("Focus %s: input driver unavailable.", active ? "gained" : "lost"); return;
        }
        const auto field = [driver](unsigned offset) { return *reinterpret_cast<const int*>(driver + offset); };
        const auto flag = [driver](unsigned offset) { return static_cast<int>(driver[offset]); };
        const auto hwnd = reinterpret_cast<HWND>(static_cast<std::uintptr_t>(field(0x218)));
        const auto display = displaySnapshot();
        RECT client{}, outer{}; POINT cursor{};
        GetClientRect(hwnd, &client); GetWindowRect(hwnd, &outer); GetCursorPos(&cursor);
        CURSORINFO shown{sizeof(shown)}; GetCursorInfo(&shown);
        log("Focus %s: input active=%d locked=%d cursorFlag=%d centre=%d,%d dinput=%d,%d; input hwnd %s render hwnd; "
            "client %ldx%ld, outer %ld,%ld-%ld,%ld, render %ux%u; cursor %ld,%ld shown=%d.",
            active ? "gained" : "lost", flag(0xFC), flag(0x21E), flag(0x21F), field(0x220), field(0x224),
            field(0xE0), field(0xE4), hwnd == display.window ? "==" : "!=", client.right, client.bottom,
            outer.left, outer.top, outer.right, outer.bottom, display.width, display.height,
            cursor.x, cursor.y, (shown.flags & CURSOR_SHOWING) != 0);
    }
    float physicalAspect(float fallback) {
        const auto display = displaySnapshot();
        RECT client{};
        if (display.managed && display.window && !IsIconic(display.window) &&
            GetClientRect(display.window,&client) && client.right > 0 && client.bottom > 0)
            return static_cast<float>(client.right) / client.bottom;
        return fallback;
    }
    bool matches(const void* address, const char* pattern) {
        const auto bytes = static_cast<const std::uint8_t*>(address);
        std::size_t index = 0;
        while (*pattern) {
            while (*pattern == ' ') ++pattern;
            if (!*pattern) break;
            if (!readable(bytes + index, 1)) return false;
            if (*pattern == '?') { while (*pattern == '?') ++pattern; }
            else {
                char* next{};
                const auto expected = std::strtoul(pattern, &next, 16);
                if (next == pattern || expected > 255 || bytes[index] != expected) return false;
                pattern = next;
            }
            ++index;
        }
        return true;
    }
    bool callTo(std::uint32_t call, std::uint32_t dest) {
        const auto code = reinterpret_cast<const std::uint8_t*>(base + call);
        return readable(code, 5) && code[0] == 0xE8 &&
            base + call + 5 + *reinterpret_cast<const std::int32_t*>(code + 1) == base + dest;
    }
    bool __cdecl isSupported(std::uint32_t w, std::uint32_t h) {
        if (plausibleMode(w, h)) return true;
        return reinterpret_cast<bool(__cdecl*)(std::uint32_t, std::uint32_t)>(supportedOriginal)(w, h);
    }
    bool __fastcall contains(void* self, void* edx, const std::uint32_t* w, const std::uint32_t* h) {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base;
        // This is a template instantiation: widen only its two measured display callers.
        if ((caller == 0x2E0DCE || caller == 0x2E0E02) && w && h && plausibleMode(*w, *h)) return true;
        using Fn = bool(__fastcall*)(void*, void*, const std::uint32_t*, const std::uint32_t*);
        return reinterpret_cast<Fn>(containsOriginal)(self, edx, w, h);
    }
    void __fastcall setViewport(void* self, void* edx, float x, float y, float w, float h) {
        UiViewport viewport{x, y, w, h};
        if (x == 0 && y == 0 && w == 1 && h == 1 && defaultTarget) {
            const auto target = static_cast<const std::uint8_t*>(defaultTarget());
            if (target && readable(target + 0x68, 4)) {
                // DI1's own SetViewport reads these fields; DI3's offsets (0x60/62) are wrong here.
                viewport = centreUi(viewport, *reinterpret_cast<const std::int16_t*>(target + 0x68),
                                     *reinterpret_cast<const std::int16_t*>(target + 0x6A),physicalAspect(0));
            }
        }
        using Fn = void(__fastcall*)(void*, void*, float, float, float, float);
        reinterpret_cast<Fn>(viewportOriginal)(self, edx, viewport.x, viewport.y, viewport.width, viewport.height);
    }
    void __fastcall setAspect(void* self, void* edx, float aspect) {
        if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base == 0xD7FB3E && defaultTarget) {
            const auto target = static_cast<const std::uint8_t*>(defaultTarget());
            if (target && readable(target + 0x78,sizeof(float))) {
                const float renderAspect = *reinterpret_cast<const float*>(target + 0x78);
                if (std::isfinite(renderAspect) && renderAspect > 0) {
                    // Native rendering already scales projection X by the target's H/W.
                    // Compensate only when the physical window has a different shape.
                    aspect = correctProjectionAspect(aspect,renderAspect,physicalAspect(renderAspect));
                }
            }
        }
        using Fn = void(__fastcall*)(void*, void*, float);
        reinterpret_cast<Fn>(aspectOriginal)(self,edx,aspect);
    }
    void __fastcall setFov(void* self, void* edx, float degrees) {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress()) - base;
        // Only Camera::ApplyProjection: never light/shadow cameras, AI FOV or camera controls.
        if ((caller == 0xD7FBB0 || caller == 0xD7FBC9) && defaultTarget) {
            const auto target = static_cast<const std::uint8_t*>(defaultTarget());
            const auto contextPointer = reinterpret_cast<const std::uint8_t* const*>(base + 0x1227108);
            const auto context = readable(contextPointer, sizeof(*contextPointer)) ? *contextPointer : nullptr;
            if (target && readable(target + 0x68, 0x14) && context && readable(context + 0xC4C, 8)) {
                const int width = *reinterpret_cast<const std::int16_t*>(target + 0x68);
                const int height = *reinterpret_cast<const std::int16_t*>(target + 0x6A);
                const float actual = *reinterpret_cast<const float*>(target + 0x78);
                const float referenceInverse = *reinterpret_cast<const float*>(context + 0xC4C);
                if (width > 0 && height > 0 && std::isfinite(actual) && actual > 0 &&
                    std::isfinite(referenceInverse) && referenceInverse > 0) {
                    // The renderer separately multiplies projection X by viewport H/W.
                    // Preserve its native CameraNode aspect instead of applying the ratio twice.
                    const float reference = 1.0f / referenceInverse;
                    const float displayAspect = physicalAspect(actual);
                    const float corrected = correctVerticalFov(degrees, actual, reference, caller == 0xD7FBB0,displayAspect);
                    static thread_local float lastAspect{};
                    if (std::abs(lastAspect - displayAspect) > 0.001f) {
                        log("Projection FOV: target %dx%d; render aspect %.5f; display aspect %.5f; native %.3f -> %.3f degrees; reference %.5f.",
                            width,height,actual,displayAspect,degrees,corrected,reference);
                        lastAspect = displayAspect;
                    }
                    degrees = corrected;
                }
            }
        }
        using Fn = void(__fastcall*)(void*, void*, float);
        reinterpret_cast<Fn>(fovOriginal)(self, edx, degrees);
    }
    bool create(std::uint32_t rva, void* detour, void** original) {
        const auto result = MH_CreateHook(reinterpret_cast<void*>(base + rva), detour, original);
        if (result != MH_OK) { log("Engine hook 0x%X refused: %s.", rva, MH_StatusToString(result)); return false; }
        return true;
    }
    bool writeKeyboardFlags(std::uint8_t flags) {
        const auto address = reinterpret_cast<std::uint8_t*>(base + keyboardFlagsRva);
        DWORD old{};
        if (!VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&old)) return false;
        *address = flags;
        DWORD unused{}; VirtualProtect(address,1,old,&unused);
        FlushInstructionCache(GetCurrentProcess(),address,1);
        return true;
    }
}
bool supportedBuild(HMODULE module) noexcept {
    const auto bytes = reinterpret_cast<const BYTE*>(module);
    if (!readable(bytes, sizeof(IMAGE_DOS_HEADER))) return false;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || dos->e_lfanew > 0x100000) return false;
    const auto pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes + dos->e_lfanew);
    return readable(pe, sizeof(*pe)) && pe->Signature == IMAGE_NT_SIGNATURE &&
        pe->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        pe->FileHeader.TimeDateStamp == 0x58535B84 &&
        pe->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC &&
        pe->OptionalHeader.SizeOfImage == 0x1511000;
}
bool installEngineHooks(HMODULE module) {
    base = reinterpret_cast<std::uintptr_t>(module);
    if (config().unlockResolutions) {
        const bool verified = matches(reinterpret_cast<void*>(base + supportedRva),
            "8B 0D ?? ?? ?? ?? 33 C0 56 85 C9 7E 1F 8B 54 24 0C 8B 74 24 08 3B 34 C5 ?? ?? ?? ?? 75 09 3B 14 C5 ?? ?? ?? ?? 74 09 40 3B C1 7C E9 32 C0 5E C3 B0 01 5E C3") &&
            matches(reinterpret_cast<void*>(base + containsRva),
            "53 56 57 E8 ?? ?? ?? ?? 33 D2 85 C0 7E 25 8B 74 24 14 8B 36 8B 49 0C 8B 7C 24 10 EB 03 8D 49 00 39 71 04 75 06 8B 19 3B 1F 74 10 42 83 C1 08 3B D0 7C ED 5F 5E 32 C0 5B C2 08 00 5F 5E B0 01 5B C2 08 00") &&
            callTo(0x24E1A, supportedRva) && callTo(0x2E0DC9, containsRva) && callTo(0x2E0DFD, containsRva);
        if (!verified) { log("REFUSED: DI1 resolution code differs; both checks left untouched."); return false; }
        if (!create(supportedRva, reinterpret_cast<void*>(&isSupported), &supportedOriginal)) return false;
        if (!create(containsRva, reinterpret_cast<void*>(&contains), &containsOriginal)) {
            MH_RemoveHook(reinterpret_cast<void*>(base + supportedRva)); return false;
        }
        MH_QueueEnableHook(reinterpret_cast<void*>(base + supportedRva));
        MH_QueueEnableHook(reinterpret_cast<void*>(base + containsRva));
        const auto enabled = MH_ApplyQueued();
        if (enabled != MH_OK) { removeEngineHooks(); log("REFUSED: resolution hook transaction failed."); return false; }
        log("Resolution checks unlocked: RVA 0x98EC0 and 0x24C60; display callers only.");
    }
    if (config().fixUiAspect || config().fixFov) {
        if (!matches(reinterpret_cast<void*>(base + targetRva), "A1 ?? ?? ?? ?? 05 A8 0C 00 00 C3") ||
            *reinterpret_cast<const std::uintptr_t*>(base + targetRva + 1) != base + 0x1227108) {
            log("REFUSED: render-target getter differs."); return false;
        }
        defaultTarget = reinterpret_cast<void*(__cdecl*)()>(base + targetRva);
    }
    if (config().fixFov) {
        const bool verified = matches(reinterpret_cast<void*>(base + fovRva),
            "8B D1 E8 E9 FF FF FF D9 44 24 04 D9 C9 DF E9 DD D8 9F F6 C4 44 7B 1E F3 0F 10 44 24 04 51 8D 8A 30 01 00 00 F3 0F 11 04 24 E8 32 45 FF FF 81 4A 38 00 10 00 00 C2 04 00") &&
            matches(reinterpret_cast<void*>(base + aspectRva),
            "8B D1 E8 E9 FF FF FF D9 44 24 04 D9 C9 DF E9 DD D8 9F F6 C4 44 7B 1E F3 0F 10 44 24 04 51 8D 8A 30 01 00 00 F3 0F 11 04 24 E8 22 45 FF FF 81 4A 38 00 10 00 00 C2 04 00") &&
            matches(reinterpret_cast<void*>(base + applyProjectionRva),
            "83 EC 10 56 8B F1 E8 ?? ?? ?? ?? D9 5C 24 10 E8 ?? ?? ?? ?? D9 5C 24 0C F3 0F 10 86 D4 03 00 00") &&
            matches(reinterpret_cast<void*>(base + 0xD7FB43),
            "8B 3D ?? ?? ?? ?? F3 0F 10 40 78 F3 0F 10 8F 50 0C 00 00 0F 2F C8 F3 0F 11 44 24 14 76 55") &&
            callTo(0xD7FB13,0xA5920) && callTo(0xD7FB26,0xA5970) && callTo(0xD7FB39,aspectRva) &&
            callTo(0xD7FB3E,targetRva) && callTo(0xD7FBAB,fovRva) && callTo(0xD7FBC4,fovRva);
        if (!verified) { log("REFUSED: camera projection profile differs; FOV left native."); return false; }
        if (!create(fovRva,reinterpret_cast<void*>(&setFov),&fovOriginal) ||
            !create(aspectRva,reinterpret_cast<void*>(&setAspect),&aspectOriginal)) return false;
        MH_QueueEnableHook(reinterpret_cast<void*>(base + fovRva));
        MH_QueueEnableHook(reinterpret_cast<void*>(base + aspectRva));
        if (MH_ApplyQueued() != MH_OK) return false;
        log("FOV/aspect correction installed: CameraNode setters RVA 0xA59C0/0xA5A10; ApplyProjection callers only; 16:9 baseline.");
    }
    if (config().fixUiAspect) {
        const bool verified = matches(reinterpret_cast<void*>(base + viewportRva),
            "83 EC 34 53 57 8B D9 33 FF 39 7B 2C 0F 84 AD 00 00 00 56 E8 ?? ?? ?? ?? 0F BF 70 68 E8 ?? ?? ?? ?? 0F BF 40 6A") &&
            callTo(viewportRva + 0x13, targetRva) && callTo(viewportRva + 0x1C, targetRva) &&
            matches(reinterpret_cast<void*>(base + targetRva), "A1 ?? ?? ?? ?? 05 A8 0C 00 00 C3");
        if (!verified) { log("UI viewport code differs; centring skipped."); return true; }
        if (create(viewportRva, reinterpret_cast<void*>(&setViewport), &viewportOriginal) &&
            MH_EnableHook(reinterpret_cast<void*>(base + viewportRva)) == MH_OK)
            log("UI centring installed: Flash::Movie::SetViewport RVA 0x3C5440; target dimensions +0x68/+0x6A.");
        else { MH_RemoveHook(reinterpret_cast<void*>(base + viewportRva)); log("UI viewport hook could not be enabled."); }
    }
    if (config().mode != Mode::Fullscreen && config().screenshotHotkeys) {
        // GUID_SysKeyboard -> SetDataFormat -> SetCooperativeLevel(0x16) -> Acquire.
        // 0x16 = NONEXCLUSIVE | FOREGROUND | NOWINKEY; retain input/pause policy, remove NOWINKEY.
        const bool verified = matches(reinterpret_cast<void*>(base + 0x9833F),
            "68 ?? ?? ?? ?? 50 8B 42 0C FF D0 85 C0 78 AC 8B 07 8B 08 8B 51 2C 68 ?? ?? ?? ?? 50 FF D2 85 C0 78 99 8B 07 8B 08 8B 51 34 6A 16 55 50 FF D2 8B 3F 8B 07 8B 48 1C 57 FF D1") &&
            *reinterpret_cast<const std::uintptr_t*>(base + 0x98340) == base + 0x1047914 &&
            matches(reinterpret_cast<void*>(base + 0x1047914),"61 2B 1D 6F A0 D5 CF 11 BF C7 44 45 53 54 00 00");
        if (!verified || !writeKeyboardFlags(0x06)) { log("REFUSED: system keyboard shortcut profile differs."); return false; }
        keyboardPatched = true;
        log("System screenshot shortcuts enabled: native keyboard remains foreground/nonexclusive; DISCL_NOWINKEY removed.");
    }
    if (config().diagnostics) setFocusObserver(&logInputState);
    return true;
}
void removeEngineHooks() {
    setFocusObserver(nullptr);
    if (keyboardPatched) { writeKeyboardFlags(0x16); keyboardPatched = false; }
    for (auto rva : {aspectRva, fovRva, viewportRva, containsRva, supportedRva}) {
        MH_DisableHook(reinterpret_cast<void*>(base + rva));
        MH_RemoveHook(reinterpret_cast<void*>(base + rva));
    }
}
}
