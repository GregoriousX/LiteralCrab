// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>

namespace crabe::di1 {
struct ImportPatch {
    void** slot{};
    void* original{};
    void* replacement{};
    bool install(HMODULE module, const char* dll, const char* name, void* detour);
    void remove();
};
bool hostCaller(const void* address) noexcept;
void setHostModule(HMODULE module) noexcept;
}
