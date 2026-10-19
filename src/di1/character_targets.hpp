// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace crabe::di1 {
// A marked ordinary target keeps the native eight-slot weapon array. The verified
// impact gate handles the additional handheld tags without growing that array.
bool installCharacterTargets(std::uintptr_t base);
void removeCharacterTargets() noexcept;
}
