// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
namespace crabe::di1 {
// Observe the native decoder and phrase-table results; never replace audio handles.
bool installAudioBankTrace(HMODULE host);
void removeAudioBankTrace();
}
