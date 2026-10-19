// SPDX-License-Identifier: GPL-3.0-only
#include "di1/audio_banks.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <atomic>
#include <cstdint>

namespace crabe::di1 {
namespace {
    constexpr unsigned bankRva = 0xD37BB0, nameRva = 0xD32360;
    using BankFn = int(__thiscall*)(void*, void*, const char*);
    using NameFn = const char*(__thiscall*)(void*, unsigned, unsigned*);
    BankFn originalBank{};
    NameFn originalName{};
    void* targets[2]{};
    unsigned created{};
    std::atomic<unsigned> phrases{};
    unsigned field(const void* data, unsigned offset) {
        const auto* p = static_cast<const unsigned char*>(data);
        return p && readable(p + offset, 2) ? *reinterpret_cast<const std::uint16_t*>(p + offset) : 0;
    }
    template <std::size_t N> void text(const void* source, char (&out)[N]) {
        const auto* p = static_cast<const char*>(source);
        for (std::size_t i = 0; p && i + 1 < N && readable(p + i, 1) && p[i]; ++i) out[i] = p[i];
    }
    int __fastcall bankHook(void* self, void*, void* bank, const char* prefix) {
        const auto result = originalBank(self, bank, prefix);
        const auto error = GetLastError();
        char locale[8]{}, name[96]{};
        if (self) text(static_cast<const char*>(self) + 0x1D4, locale);
        if (bank) text(static_cast<const char*>(bank) + 0x40, name);
        const void* names{};
        if (!result && bank && readable(bank, 16)) names = *reinterpret_cast<void**>(static_cast<char*>(bank) + 12);
        log("Native audio bank: language=%s bank=%s result=%d NAMEversion=%u family=%u samples=%u.",
            locale, name, result, field(names, 0), field(names, 2), field(names, 10));
        SetLastError(error);
        return result;
    }
    const char* __fastcall nameHook(void* self, void*, unsigned phrase, unsigned* index) {
        const auto* result = originalName(self, phrase, index);
        const auto error = GetLastError();
        const auto event = phrases.fetch_add(1, std::memory_order_relaxed);
        if (event < 256) {
            char label[96]{};
            text(result, label);
            const auto sample = result && index && readable(index, sizeof(*index)) ? *index : ~0u;
            log("Native audio phrase: family=%u phrase=%u bounds=%u..%u sample=%u found=%u label=%s.",
                field(self, 2), phrase, field(self, 6), field(self, 8), sample, result != nullptr, label);
            if (event == 255) log("Native audio phrase trace: session limit reached (256 phrases); further phrase diagnostics omitted; native lookups continue.");
        }
        SetLastError(error);
        return result;
    }
}
void removeAudioBankTrace() {
    for (unsigned i = 0; i < created; ++i) { MH_DisableHook(targets[i]); MH_RemoveHook(targets[i]); }
    created = 0;
}
bool installAudioBankTrace(HMODULE host) {
    if (created) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    if (!codeMatches(base + bankRva, "81 EC 20 01 00 00 8B 84 24 28 01 00 00 53 55 56 57") ||
        !codeMatches(base + nameRva, "56 8B F1 0F B7 4E 06 57 8B 7C 24 0C 33 C0 3B F9 72 7B") ||
        !callsTo(base, 0xD37BEE, 0xD32230) || !callsTo(base, 0xD368DD, nameRva)) {
        log("Native audio bank trace: profile differs; no diagnostics installed.");
        return false;
    }
    targets[0] = reinterpret_cast<void*>(base + bankRva);
    targets[1] = reinterpret_cast<void*>(base + nameRva);
    void* detours[] = {reinterpret_cast<void*>(&bankHook), reinterpret_cast<void*>(&nameHook)};
    void** originals[] = {reinterpret_cast<void**>(&originalBank), reinterpret_cast<void**>(&originalName)};
    for (unsigned i = 0; i < 2; ++i) {
        if (MH_CreateHook(targets[i], detours[i], originals[i]) != MH_OK) { removeAudioBankTrace(); return false; }
        ++created;
    }
    for (unsigned i = 0; i < 2; ++i)
        if (MH_EnableHook(targets[i]) != MH_OK) { removeAudioBankTrace(); return false; }
    phrases = 0;
    log("Native audio bank and phrase diagnostics enabled; native results preserved.");
    return true;
}
}
