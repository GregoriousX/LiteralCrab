// SPDX-License-Identifier: GPL-3.0-only
// Points the process's file opens and attribute queries for replaced assets at the mod or cache
// copy. Hooks kernel32 so sound and video libraries are covered as well as the game itself.
#pragma once
#include "mods/asset_mods.hpp"

namespace crabe::mods {
using LogFunction = void (*)(const char* format, ...);
// Installs the hooks once; the index is never modified afterwards. Returns false and leaves
// nothing hooked on failure. Requires MinHook to be initialized.
bool installFileRedirect(AssetIndex index, LogFunction log, bool trace);
// The replacement for a file name, or null. Exposed for tests.
const std::wstring* redirectedAsset(const char* fileName) noexcept;
const std::wstring* redirectedAsset(const wchar_t* fileName) noexcept;
void setAssetIndexForTest(AssetIndex index);
}
