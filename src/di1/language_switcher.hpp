// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/common.hpp"
#include "di1/asset_loader.hpp"

namespace crabe::di1 {
// Install after the resolved mod asset provider, before native locale/audio startup.
// Disabled settings leave both native calls untouched; no registry writes or polling.
bool installLanguageSwitcher(HMODULE host, const Config& options, AssetProvider assets);
void removeLanguageSwitcher() noexcept;
}
