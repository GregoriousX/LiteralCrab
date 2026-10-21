// SPDX-License-Identifier: GPL-3.0-only
// Holds the asset index for the lifetime of the process. Points file opens and attribute queries
// for added/replaced files and their directories at the mod or cache copy (kernel32 hooks, so sound and video
// libraries are covered too), and serves files the engine's loader requests by name.
#pragma once
#include "mods/asset_mods.hpp"

namespace crabe::mods {
using LogFunction = void (*)(const char* format, ...);
// Call once; the index is never modified afterwards. File hooks are installed only when the
// index has redirects. Returns false and leaves nothing hooked on failure. Requires MinHook.
// Successful dialogue opens are quiet unless either trace option is explicit.
bool installFileRedirect(AssetIndex index, LogFunction log, bool trace, bool traceAudioFiles = false);
// The engine-loader provider: the mod's data for a request name. Thread-safe.
bool readAssetMember(const char* name, std::string& data);
// The replacement for a file name, or null. Exposed for tests.
const std::wstring* redirectedAsset(const char* fileName) noexcept;
const std::wstring* redirectedAsset(const wchar_t* fileName) noexcept;
void setAssetIndexForTest(AssetIndex index);
}
