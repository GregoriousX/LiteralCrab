// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>

namespace crabe::di1 {
bool installCharacterParticles(std::uintptr_t base, bool diagnostics);
void removeCharacterParticles() noexcept;
}
