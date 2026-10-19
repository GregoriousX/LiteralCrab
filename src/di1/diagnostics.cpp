// SPDX-License-Identifier: GPL-3.0-only
#include "di1/diagnostics.hpp"
#include "di1/common.hpp"
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>

namespace crabe::di1 {
namespace {
// Hash locally: initialization can run under the ASI loader's DllMain, where
// asking a cryptography provider to load another DLL would be unsafe.
struct Sha256 {
    DWORD state[8] = {0x6A09E667,0xBB67AE85,0x3C6EF372,0xA54FF53A,0x510E527F,0x9B05688C,0x1F83D9AB,0x5BE0CD19};
    BYTE block[64]{};
    unsigned fill{};
    std::uint64_t count{};
    static DWORD rotate(DWORD value, unsigned bits) { return (value >> bits) | (value << (32 - bits)); }
    void compress() noexcept {
        static constexpr DWORD constants[64] = {
            0x428A2F98,0x71374491,0xB5C0FBCF,0xE9B5DBA5,0x3956C25B,0x59F111F1,0x923F82A4,0xAB1C5ED5,
            0xD807AA98,0x12835B01,0x243185BE,0x550C7DC3,0x72BE5D74,0x80DEB1FE,0x9BDC06A7,0xC19BF174,
            0xE49B69C1,0xEFBE4786,0x0FC19DC6,0x240CA1CC,0x2DE92C6F,0x4A7484AA,0x5CB0A9DC,0x76F988DA,
            0x983E5152,0xA831C66D,0xB00327C8,0xBF597FC7,0xC6E00BF3,0xD5A79147,0x06CA6351,0x14292967,
            0x27B70A85,0x2E1B2138,0x4D2C6DFC,0x53380D13,0x650A7354,0x766A0ABB,0x81C2C92E,0x92722C85,
            0xA2BFE8A1,0xA81A664B,0xC24B8B70,0xC76C51A3,0xD192E819,0xD6990624,0xF40E3585,0x106AA070,
            0x19A4C116,0x1E376C08,0x2748774C,0x34B0BCB5,0x391C0CB3,0x4ED8AA4A,0x5B9CCA4F,0x682E6FF3,
            0x748F82EE,0x78A5636F,0x84C87814,0x8CC70208,0x90BEFFFA,0xA4506CEB,0xBEF9A3F7,0xC67178F2};
        DWORD words[64]{};
        for (unsigned i = 0; i < 16; ++i)
            words[i] = (DWORD(block[i*4]) << 24) | (DWORD(block[i*4+1]) << 16) | (DWORD(block[i*4+2]) << 8) | block[i*4+3];
        for (unsigned i = 16; i < 64; ++i) {
            const DWORD x = words[i-15], y = words[i-2];
            words[i] = words[i-16] + (rotate(x,7)^rotate(x,18)^(x>>3)) + words[i-7] + (rotate(y,17)^rotate(y,19)^(y>>10));
        }
        DWORD a=state[0],b=state[1],c=state[2],d=state[3],e=state[4],f=state[5],g=state[6],h=state[7];
        for (unsigned i = 0; i < 64; ++i) {
            const DWORD t1 = h + (rotate(e,6)^rotate(e,11)^rotate(e,25)) + ((e&f)^(~e&g)) + constants[i] + words[i];
            const DWORD t2 = (rotate(a,2)^rotate(a,13)^rotate(a,22)) + ((a&b)^(a&c)^(b&c));
            h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
        }
        state[0]+=a; state[1]+=b; state[2]+=c; state[3]+=d; state[4]+=e; state[5]+=f; state[6]+=g; state[7]+=h;
    }
    void add(const BYTE* bytes, unsigned length) noexcept {
        count += length;
        while (length--) { block[fill++] = *bytes++; if (fill == 64) { compress(); fill = 0; } }
    }
    void finish(BYTE (&result)[32]) noexcept {
        const std::uint64_t bits = count * 8;
        const BYTE start = 0x80, zero = 0;
        add(&start, 1);
        while (fill != 56) add(&zero, 1);
        BYTE size[8];
        for (unsigned i = 0; i < 8; ++i) size[i] = static_cast<BYTE>(bits >> ((7-i)*8));
        add(size, 8);
        for (unsigned i = 0; i < 32; ++i) result[i] = static_cast<BYTE>(state[i/4] >> ((3-i%4)*8));
    }
};
HANDLE exceptionFile = INVALID_HANDLE_VALUE;
PVOID observer{};
volatile LONG busy{}, eventCount{};
constexpr LONG maximumEvents = 256;
// Static scratch avoids large stack use. The observer never waits for this
// buffer, uses the CRT, allocates, or takes the normal logger's lock.
char output[6144];
unsigned used{};
std::uintptr_t hostBase{}, pluginBase{};
void literal(const char* text) noexcept {
    while (*text && used < sizeof(output)) output[used++] = *text++;
}
void hex(std::uintptr_t value) noexcept {
    literal("0x");
    for (int shift = 28; shift >= 0 && used < sizeof(output); shift -= 4)
        output[used++] = "0123456789ABCDEF"[(value >> shift) & 15];
}
void field(const char* name, std::uintptr_t value) noexcept {
    literal(name); hex(value); literal(" ");
}
bool serious(DWORD code) noexcept {
    // Exhausted stacks and fail-fast need the external collector. C++/debug/
    // guard exceptions are quiet. Observed first-chance events can be handled.
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
        code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_PRIV_INSTRUCTION ||
        code == EXCEPTION_INT_DIVIDE_BY_ZERO || code == 0xC0000374;
}
bool stackWord(std::uintptr_t address, DWORD& value) noexcept {
    MEMORY_BASIC_INFORMATION region{};
    // Never consume a guard page just to read diagnostics.
    if (!VirtualQuery(reinterpret_cast<void*>(address), &region, sizeof(region)) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_NOACCESS | PAGE_GUARD))) return false;
    const auto end = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize;
    if (address > end || end - address < sizeof(value)) return false;
    __try { value = *reinterpret_cast<const DWORD*>(address); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
LONG CALLBACK observeException(EXCEPTION_POINTERS* pointers) noexcept {
    const DWORD savedError = GetLastError();
    if (!pointers || !pointers->ExceptionRecord || !pointers->ContextRecord ||
        !serious(pointers->ExceptionRecord->ExceptionCode) || exceptionFile == INVALID_HANDLE_VALUE ||
        InterlockedCompareExchange(&busy, 1, 0)) {
        SetLastError(savedError); return EXCEPTION_CONTINUE_SEARCH;
    }
    if (eventCount < maximumEvents) {
        ++eventCount;
        const auto& record = *pointers->ExceptionRecord;
        const auto& context = *pointers->ContextRecord;
        used = 0;
        literal("\r\nFIRST_CHANCE (may be handled; not proof of crash) ");
        field("event=", static_cast<DWORD>(eventCount));
        field("pid=", GetCurrentProcessId()); field("tid=", GetCurrentThreadId());
        field("tick=", GetTickCount()); literal("\r\n");
        SYSTEMTIME now{}; GetLocalTime(&now);
        field("local_hour=", now.wHour); field("minute=", now.wMinute);
        field("second=", now.wSecond); field("millisecond=", now.wMilliseconds); literal("\r\n");
        field("code=", record.ExceptionCode);
        field("address=", reinterpret_cast<std::uintptr_t>(record.ExceptionAddress));
        field("flags=", record.ExceptionFlags); literal("\r\n");
        for (DWORD i = 0; i < record.NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; ++i)
            field("parameter=", record.ExceptionInformation[i]);
        literal("\r\n");
        field("eip=", context.Eip); field("esp=", context.Esp); field("ebp=", context.Ebp);
        field("eflags=", context.EFlags); literal("\r\n");
        field("eax=", context.Eax); field("ebx=", context.Ebx); field("ecx=", context.Ecx);
        field("edx=", context.Edx); literal("\r\n");
        field("esi=", context.Esi); field("edi=", context.Edi);
        field("host_base=", hostBase); field("asi_base=", pluginBase); literal("\r\n");
        MEMORY_BASIC_INFORMATION region{};
        if (VirtualQuery(reinterpret_cast<void*>(context.Eip), &region, sizeof(region))) {
            const auto base = reinterpret_cast<std::uintptr_t>(region.AllocationBase);
            field("eip_allocation=", base); field("eip_offset=", context.Eip - base);
            field("memory_type=", region.Type); field("protection=", region.Protect); literal("\r\n");
        }
        literal("RAW_STACK_WORDS (data and return-address candidates; not an unwound stack)\r\n");
        for (unsigned i = 0; i < 64; ++i) {
            if (context.Esp > UINTPTR_MAX - i * sizeof(DWORD)) break;
            const auto address = context.Esp + i * sizeof(DWORD);
            DWORD value{};
            if (!stackWord(address, value)) { literal("unreadable stack boundary\r\n"); break; }
            hex(value); literal(i % 8 == 7 ? "\r\n" : " ");
        }
        literal("\r\n");
        if (eventCount == maximumEvents) literal("SESSION LIMIT reached: further first-chance events omitted. Automatic top-level crash reports have an independent budget.\r\n");
        DWORD written{};
        WriteFile(exceptionFile, output, used, &written, nullptr);
    }
    InterlockedExchange(&busy, 0);
    SetLastError(savedError);
    // Never recover, swallow, retry, change CONTEXT or replace the game's SEH.
    return EXCEPTION_CONTINUE_SEARCH;
}
void writeText(const char* text) {
    DWORD written{};
    WriteFile(exceptionFile, text, static_cast<DWORD>(std::strlen(text)), &written, nullptr);
}
void identities(const wchar_t* path, const char* label) {
    char digest[65]{};
    if (diagnosticFileSha256(path, digest)) {
        log("Diagnostics: %s SHA256=%s.", label, digest);
        if (exceptionFile != INVALID_HANDLE_VALUE) {
            char line[180]{};
            sprintf_s(line, "%s SHA256=%s\r\n", label, digest); writeText(line);
        }
    } else log("Diagnostics: %s SHA256 unavailable (Win32 %lu).", label, GetLastError());
}
}
bool diagnosticFileSha256(const wchar_t* path, char (&digest)[65]) noexcept {
    digest[0] = '\0';
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    Sha256 hash;
    bool okay = true;
    BYTE buffer[16384], result[32]; DWORD bytes{};
    while (okay) {
        if (!ReadFile(file, buffer, sizeof(buffer), &bytes, nullptr)) { okay = false; break; }
        if (!bytes) break;
        hash.add(buffer, bytes);
    }
    if (okay) hash.finish(result);
    const DWORD error = GetLastError();
    CloseHandle(file);
    if (okay) {
        for (unsigned i = 0; i < sizeof(result); ++i) {
            digest[i * 2] = "0123456789ABCDEF"[result[i] >> 4];
            digest[i * 2 + 1] = "0123456789ABCDEF"[result[i] & 15];
        }
        digest[64] = '\0';
    }
    SetLastError(error);
    return okay;
}
bool openExceptionDiagnostics(const wchar_t* path, HMODULE host, HMODULE plugin) {
    if (observer) return true;
    exceptionFile = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (exceptionFile == INVALID_HANDLE_VALUE) return false;
    hostBase = reinterpret_cast<std::uintptr_t>(host); pluginBase = reinterpret_cast<std::uintptr_t>(plugin);
    eventCount = 0;
    SYSTEMTIME now{}; GetLocalTime(&now);
    char line[400]{};
    sprintf_s(line, "\r\nSESSION %04u-%02u-%02u %02u:%02u:%02u pid=%lu build=" __DATE__ " " __TIME__
        "\r\nFirst-chance observer; 256 events/session. Handled events are not crashes. Automatic top-level crash reports are independent; fail-fast/termination can bypass both.\r\n",
        now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, GetCurrentProcessId());
    writeText(line);
    sprintf_s(line, "host_base=%p asi_base=%p; automatic crash dumps contain module identities.\r\n", host, plugin);
    writeText(line);
    observer = AddVectoredExceptionHandler(1, observeException);
    if (observer) return true;
    CloseHandle(exceptionFile); exceptionFile = INVALID_HANDLE_VALUE;
    return false;
}
void removeExceptionDiagnostics() {
    if (observer) { RemoveVectoredExceptionHandler(observer); observer = nullptr; }
    if (exceptionFile != INVALID_HANDLE_VALUE) { CloseHandle(exceptionFile); exceptionFile = INVALID_HANDLE_VALUE; }
}
void installDiagnostics(HMODULE host, HMODULE plugin, const wchar_t* directory, bool exceptions) {
    log("Diagnostics: build=" __DATE__ " " __TIME__ "; revision=qa-20261019-seamless-v2.");
    if (exceptions && !openExceptionDiagnostics((std::wstring(directory) + L"LiteralCrab-exceptions.log").c_str(), host, plugin))
        log("Diagnostics: could not open first-chance exception observer (Win32 %lu).", GetLastError());
    if (observer) log("Diagnostics: first-chance observer active; LiteralCrab-exceptions.log (handled events are not crashes).");
    wchar_t path[32768]{};
    DWORD count = GetModuleFileNameW(host, path, _countof(path));
    if (count && count < _countof(path)) identities(path, "EXE");
    count = GetModuleFileNameW(plugin, path, _countof(path));
    if (count && count < _countof(path)) identities(path, "ASI");
    identities((std::wstring(directory) + L"LiteralCrab.ini").c_str(), "INI");
}
}
