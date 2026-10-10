// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <string>

namespace crabe::di1 {
// Supplies the replacement for a file the engine loads by name; false keeps the engine's own.
// Called on the engine's loader threads.
using AssetProvider = bool (*)(const char* name, std::string& data);

// Hooks the file thread's in-memory cache checks, which every whole-file load passes first.
// A provided file is served exactly like a cache hit: the engine's own allocator and heap,
// the same status fields and completion signal. trace logs every request name.
bool installAssetLoader(HMODULE host, AssetProvider provider, bool trace);
}
