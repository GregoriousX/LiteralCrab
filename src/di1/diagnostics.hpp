// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>

namespace crabe::di1 {
struct Config;
// Configure optional observers only after the ASI has been pinned. The crash
// writer still starts from the post-loader graphics callback, never here.
void installDiagnosticStartup(HMODULE host, HMODULE plugin, const wchar_t* directory, const Config& options);
void installDiagnostics(HMODULE host, HMODULE plugin, const wchar_t* directory, bool exceptions);
bool openExceptionDiagnostics(const wchar_t* path, HMODULE host, HMODULE plugin);
void removeExceptionDiagnostics();
bool diagnosticFileSha256(const wchar_t* path, char (&digest)[65]) noexcept;
}
