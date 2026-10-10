// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include "di1/policy.hpp"

namespace crabe::di1 {
enum class VSync { Auto, Off, On };
struct Config {
    Mode mode = Mode::Borderless;
    bool unlockResolutions = true;
    bool fixUiAspect = true;
    bool fixFov = true;
    bool screenshotHotkeys = true;
    bool keepVisibleOnFocusLoss = true;
    bool scaleMouse = true;
    bool altEnter = true;
    bool rememberMode = true;
    unsigned frameLimit = 120;
    VSync vsync = VSync::Auto;
    bool diagnostics = false;
    bool mods = true;
    bool modTrace = false;
    // Seconds before the camera returns behind the character after the player moves it;
    // negative keeps each camera's own value.
    float cameraReturnDelay = -1.0f;
    // Post-processing effects the scene settings may enable; false forces them off everywhere.
    bool motionBlur = true;
    bool depthOfField = true;
};
void openLogAndConfig(HMODULE module);
const Config& config() noexcept;
// Directory holding the ASI and LiteralCrab.ini, with a trailing backslash.
const wchar_t* pluginDirectory() noexcept;
void saveMode(Mode mode);
void log(const char* format, ...);
bool readable(const void* address, std::size_t length) noexcept;
// Code signature check: space-separated hex bytes, "??" for bytes that differ per load (the
// game is relocated, so absolute addresses inside instructions are wildcarded).
bool codeMatches(std::uintptr_t address, const char* pattern) noexcept;
// True when the instruction at base+site is a relative call to base+target.
bool callsTo(std::uintptr_t base, std::uint32_t site, std::uint32_t target) noexcept;
}
