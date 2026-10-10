// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstddef>
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
};
void openLogAndConfig(HMODULE module);
const Config& config() noexcept;
void saveMode(Mode mode);
void log(const char* format, ...);
bool readable(const void* address, std::size_t length) noexcept;
}
