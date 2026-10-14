// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstddef>
#include <cstdint>
#include <string>
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
    bool bloom = true;
    // Exchange the X and Y buttons of XInput controllers.
    bool swapXY = false;
    // Multiplier for the Toy Box editor's usage limits (the "memory" meter); 1 keeps the game's.
    unsigned toyBoxLimitScale = 1;
    // Let every character play in every play set (needs mods enabled for ownership/FX data).
    bool charactersInAnyPlayset = false;
    bool traceCharacterFreedom = false;
    // Temporary character-freedom testing aid; native rewards remain unchanged at 1.
    unsigned testExperienceMultiplier = 1;
    // Start without the logo movies.
    bool skipIntro = false;
    // off keeps native startup/Steam language selection. Other values name a locale.
    std::wstring languageSwitcher = L"off";
    // Toy Box Share with the Toy Boxes of the toyboxes folder and of these servers (URLs).
    bool toyBoxShare = false;
    std::wstring toyBoxServers;
    // Safety limits for packages: Toy Boxes listed, one server package, the whole server mirror.
    unsigned toyBoxMaximum = 5000, toyBoxServerPackageMB = 32, toyBoxServerMirrorMB = 1024;
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
