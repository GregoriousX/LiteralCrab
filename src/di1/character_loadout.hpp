// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/figure_registry.hpp"
#include <windows.h>

namespace crabe::di1 {
using ToolInitializationCallback = void (*)(void*) noexcept;
using ToolInitializationRegistrar = bool (*)(ToolInitializationCallback) noexcept;
// Makes declared Item/Prop slots selectable on the private DNA instance only.
// Existing unlock, suppression, group and use checks remain native.
bool installCustomLoadouts(HMODULE host, const std::vector<CustomFigure>& figures,
    ToolInitializationRegistrar existing = nullptr);
void removeCustomLoadouts() noexcept;
}
