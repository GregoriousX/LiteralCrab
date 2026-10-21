// SPDX-License-Identifier: GPL-3.0-only
#include "di1/audio_banks.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {
constexpr unsigned bankRva = 0xD37BB0, nameRva = 0xD32360;
constexpr DWORD nativeError = 0x1234ABCD, loggerError = 0x5678DCBA;
using BankFn = int(__thiscall*)(void*, void*, const char*);
using NameFn = const char*(__thiscall*)(void*, unsigned, unsigned*);
void* seenSelf{};
void* seenBank{};
const char* seenPrefix{};
unsigned seenPhrase{}, bankCalls{}, nameCalls{};
unsigned* seenIndex{};
int bankResult{};
const char* nameResult{};
unsigned sampleResult = 137;
std::vector<std::string> messages;

int __fastcall captureBank(void* self, void*, void* bank, const char* prefix) {
    seenSelf = self; seenBank = bank; seenPrefix = prefix; ++bankCalls;
    SetLastError(nativeError);
    return bankResult;
}
const char* __fastcall captureName(void* self, void*, unsigned phrase, unsigned* index) {
    seenSelf = self; seenPhrase = phrase; seenIndex = index; ++nameCalls;
    if (nameResult && index) *index = sampleResult;
    SetLastError(nativeError);
    return nameResult;
}
// Observe diagnostics without opening the production log or touching its config.
// Deliberately change LastError: both wrappers must restore the native value.
void __cdecl captureLog(const char* format, ...) {
    char buffer[2048]{};
    va_list args;
    va_start(args, format);
    std::vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    messages.emplace_back(buffer);
    SetLastError(loggerError);
}
bool write(void* destination, const void* source, std::size_t size) {
    DWORD old{};
    if (!VirtualProtect(destination, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(destination, source, size);
    const bool restored = VirtualProtect(destination, size, old, &old) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), destination, size);
    return restored;
}
void relativeCall(unsigned char* base, unsigned site, unsigned target) {
    base[site] = 0xE8;
    const auto relative = static_cast<std::int32_t>(target - site - 5);
    std::memcpy(base + site + 1, &relative, sizeof(relative));
}
template <std::size_t N> void jumpTail(std::array<unsigned char, N>& bytes,
    unsigned char* destination, std::uintptr_t target) {
    static_assert(N >= 5);
    bytes[N - 5] = 0xE9;
    const auto relative = static_cast<std::int32_t>(target -
        reinterpret_cast<std::uintptr_t>(destination + N));
    std::memcpy(bytes.data() + N - 4, &relative, sizeof(relative));
}
bool contains(const std::string& message) {
    for (const auto& value : messages) if (value.find(message) != std::string::npos) return true;
    return false;
}
}

int wmain(int argc, wchar_t** argv) {
    static_assert(sizeof(void*) == 4, "Native audio ABI tests require the DI1 x86 target.");
    using namespace crabe::di1;
    unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
    HMODULE host{};
    unsigned char* base{};
    unsigned codeRva{}, codeSize{};
    if (argc >= 2) {
        // Private mapping only: never execute the game's entry point or write its file.
        host = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(host != nullptr);
        if (!host) return 1;
        base = reinterpret_cast<unsigned char*>(host);
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base +
            reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
        const auto* sections = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
            if (!std::memcmp(sections[i].Name, ".text", 5)) {
                codeRva = sections[i].VirtualAddress;
                codeSize = sections[i].Misc.VirtualSize;
            }
        CHECK(codeSize != 0);
    } else {
        // CTest still exercises real MinHook trampolines and x86 calling conventions
        // without requiring a game installation. The optional EXE argument additionally
        // verifies that the signatures and rollback match the shipped executable.
        codeSize = 0xD40000;
        base = static_cast<unsigned char*>(VirtualAlloc(nullptr, codeSize,
            MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
        CHECK(base != nullptr);
        if (!base) return 1;
        host = reinterpret_cast<HMODULE>(base);
        const unsigned char bankBytes[] = {
            0x81, 0xEC, 0x20, 0x01, 0, 0, 0x8B, 0x84, 0x24, 0x28, 0x01, 0, 0, 0x53, 0x55, 0x56, 0x57};
        const unsigned char nameBytes[] = {
            0x56, 0x8B, 0xF1, 0x0F, 0xB7, 0x4E, 0x06, 0x57, 0x8B, 0x7C, 0x24, 0x0C,
            0x33, 0xC0, 0x3B, 0xF9, 0x72, 0x7B};
        std::memcpy(base + bankRva, bankBytes, sizeof(bankBytes));
        std::memcpy(base + nameRva, nameBytes, sizeof(nameBytes));
        relativeCall(base, 0xD37BEE, 0xD32230);
        relativeCall(base, 0xD368DD, nameRva);
    }
    if (!codeSize) { FreeLibrary(host); return 1; }
    const std::vector<unsigned char> snapshot(base + codeRva, base + codeRva + codeSize);
    const auto unchanged = [&] { return !std::memcmp(base + codeRva, snapshot.data(), snapshot.size()); };
    const auto restore = [&](unsigned rva, std::size_t size) {
        CHECK(write(base + rva, snapshot.data() + rva - codeRva, size));
    };
    // An available native profile or mod-supplied bank must not opt into traces.
    // This succeeds before MinHook initialization and leaves all native bytes.
    CHECK(!config().traceAudioBanks);
    CHECK(installAudioBankTrace(host));
    CHECK(unchanged());
    const_cast<Config&>(config()).traceAudioBanks = true;
    CHECK(MH_Initialize() == MH_OK);
    void* logTrampoline{};
    const auto logTarget = reinterpret_cast<void*>(&crabe::di1::log);
    const bool loggerCreated = MH_CreateHook(logTarget, reinterpret_cast<void*>(&captureLog), &logTrampoline) == MH_OK;
    CHECK(loggerCreated);
    const bool loggerEnabled = loggerCreated && MH_EnableHook(logTarget) == MH_OK;
    CHECK(loggerEnabled);

    // Each profile disagreement must leave both entry points and the other patch intact.
    for (const auto rva : {bankRva, nameRva, 0xD37BEFu, 0xD368DEu}) {
        const unsigned char different = base[rva] ^ 1;
        CHECK(write(base + rva, &different, 1));
        const std::vector<unsigned char> disagreed(base + codeRva, base + codeRva + codeSize);
        CHECK(!installAudioBankTrace(host));
        CHECK(!std::memcmp(base + codeRva, disagreed.data(), disagreed.size()));
        removeAudioBankTrace();
        CHECK(!std::memcmp(base + codeRva, disagreed.data(), disagreed.size()));
        restore(rva, 1);
    }
    CHECK(unchanged());

    // Failure creating the second hook rolls back the first, while an unrelated
    // owner's existing hook remains registered and can still be enabled and called.
    void* occupiedTrampoline{};
    CHECK(MH_CreateHook(base + nameRva, reinterpret_cast<void*>(&captureName), &occupiedTrampoline) == MH_OK);
    CHECK(!installAudioBankTrace(host));
    CHECK(unchanged());
    CHECK(MH_EnableHook(base + nameRva) == MH_OK);
    alignas(4) std::array<unsigned char, 32> names{};
    const unsigned short header[] = {4, 0x5246, 5, 100, 104, 37};
    std::memcpy(names.data(), header, sizeof(header));
    unsigned index = 0xAABBCCDD;
    nameResult = "FR001";
    const auto lookup = reinterpret_cast<NameFn>(base + nameRva);
    CHECK(lookup(names.data(), 101, &index) == nameResult && index == sampleResult);
    CHECK(MH_DisableHook(base + nameRva) == MH_OK);
    CHECK(MH_RemoveHook(base + nameRva) == MH_OK);
    CHECK(unchanged());

    CHECK(installAudioBankTrace(host));
    CHECK(installAudioBankTrace(host)); // An already-owned installation is idempotent.
    // The first complete original instructions copied by MinHook are:
    // bank: sub esp,0x120 (6 bytes); name: push esi; mov esi,ecx; movzx ecx,[esi+6] (7).
    // Restore their stack/register effects before entering the capture implementations.
    std::array<unsigned char, 11> bankContinuation{0x81, 0xC4, 0x20, 0x01, 0, 0};
    jumpTail(bankContinuation, base + bankRva + 6, reinterpret_cast<std::uintptr_t>(&captureBank));
    CHECK(write(base + bankRva + 6, bankContinuation.data(), bankContinuation.size()));
    std::array<unsigned char, 8> nameContinuation{0x8B, 0xCE, 0x5E};
    jumpTail(nameContinuation, base + nameRva + 7, reinterpret_cast<std::uintptr_t>(&captureName));
    CHECK(write(base + nameRva + 7, nameContinuation.data(), nameContinuation.size()));

    alignas(4) std::array<unsigned char, 0x200> audio{};
    alignas(4) std::array<unsigned char, 0xA0> bank{};
    std::memcpy(audio.data() + 0x1D4, "RUR", 4);
    std::memcpy(bank.data() + 0x40, "Francesco_str", 14);
    void* namesPointer = names.data();
    std::memcpy(bank.data() + 12, &namesPointer, sizeof(namesPointer));
    const auto load = reinterpret_cast<BankFn>(base + bankRva);
    static const char prefix[] = "native prefix";
    bankCalls = nameCalls = 0;
    messages.clear();
    bankResult = 0;
    CHECK(load(audio.data(), bank.data(), prefix) == 0);
    CHECK(GetLastError() == nativeError);
    CHECK(bankCalls == 1 && seenSelf == audio.data() && seenBank == bank.data() && seenPrefix == prefix);
    CHECK(contains("language=RUR bank=Francesco_str result=0 NAMEversion=4 family=21062 samples=37"));

    // A readable stale NAME pointer after native failure must not contribute any
    // diagnostic fields. Preserve the full int error, rather than collapsing to bool.
    for (const int result : {0x52, 0x4B, 0x2B, -1234567}) {
        messages.clear(); bankResult = result;
        CHECK(load(audio.data(), bank.data(), nullptr) == result);
        CHECK(GetLastError() == nativeError && seenPrefix == nullptr);
        CHECK(contains("NAMEversion=0 family=0 samples=0"));
    }
    bankResult = 0;
    CHECK(load(nullptr, nullptr, nullptr) == 0 && seenSelf == nullptr && seenBank == nullptr);
    CHECK(GetLastError() == nativeError);

    messages.clear();
    nameResult = "FR001_returned_label";
    index = 0xAABBCCDD;
    CHECK(lookup(names.data(), 0x12345678, &index) == nameResult);
    CHECK(GetLastError() == nativeError);
    CHECK(seenSelf == names.data() && seenPhrase == 0x12345678 && seenIndex == &index && index == sampleResult);
    CHECK(contains("phrase=305419896 bounds=100..104 sample=137 found=1 label=FR001_returned_label"));
    CHECK(lookup(names.data(), 102, nullptr) == nameResult && seenIndex == nullptr);
    CHECK(GetLastError() == nativeError);
    nameResult = nullptr;
    index = 0xAABBCCDD;
    CHECK(lookup(names.data(), 900, &index) == nullptr && index == 0xAABBCCDD);
    CHECK(GetLastError() == nativeError);
    CHECK(contains("phrase=900 bounds=100..104 sample=4294967295 found=0 label="));

    // Emit the limit marker exactly with the 256th phrase, then stop logging
    // while native lookups, writes and full pointer results continue unchanged.
    // Include the three calls above in the exact phrase log limit.
    nameResult = "FR002";
    for (unsigned i = 0; i < 252; ++i) {
        CHECK(lookup(names.data(), i, &index) == nameResult && index == sampleResult);
        CHECK(GetLastError() == nativeError);
    }
    CHECK(nameCalls == 255 && messages.size() == 255);
    CHECK(!contains("Native audio phrase trace: session limit reached"));
    CHECK(lookup(names.data(), 252, &index) == nameResult && index == sampleResult);
    CHECK(GetLastError() == nativeError);
    CHECK(nameCalls == 256 && messages.size() == 257);
    if (messages.size() == 257) {
        CHECK(messages[255].find("Native audio phrase: ") == 0);
        CHECK(messages[256] == "Native audio phrase trace: session limit reached (256 phrases); further phrase diagnostics omitted; native lookups continue.");
    }
    for (unsigned i = 253; i < 300; ++i) {
        CHECK(lookup(names.data(), i, &index) == nameResult && index == sampleResult);
        CHECK(GetLastError() == nativeError);
    }
    CHECK(nameCalls == 303 && messages.size() == 257);

    restore(bankRva + 6, bankContinuation.size());
    restore(nameRva + 7, nameContinuation.size());
    removeAudioBankTrace();
    CHECK(unchanged());
    removeAudioBankTrace();
    CHECK(unchanged());
    // Reinstalling after removal must reset the phrase diagnostic cap.
    messages.clear();
    CHECK(installAudioBankTrace(host));
    CHECK(write(base + nameRva + 7, nameContinuation.data(), nameContinuation.size()));
    messages.clear();
    CHECK(lookup(names.data(), 102, &index) == nameResult);
    CHECK(messages.size() == 1 && contains("found=1 label=FR002"));
    CHECK(!contains("Native audio phrase trace: session limit reached"));
    restore(nameRva + 7, nameContinuation.size());
    removeAudioBankTrace();
    CHECK(unchanged());

    if (loggerCreated) { CHECK(MH_DisableHook(logTarget) == MH_OK); CHECK(MH_RemoveHook(logTarget) == MH_OK); }
    CHECK(MH_Uninitialize() == MH_OK);
    if (argc >= 2) FreeLibrary(host);
    else CHECK(VirtualFree(base, 0, MEM_RELEASE) != FALSE);
    std::printf("Audio bank %s ABI/results/diagnostics/rollback: %s\n",
        argc >= 2 ? "original executable" : "private fixture", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
