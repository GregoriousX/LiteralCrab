// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_targets.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <windows.h>
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <cstdarg>
#include <string>

namespace {
int failures{};
std::vector<std::string> messages;
void __cdecl traceLog(const char* format,...) {
    char line[2048]{}; va_list args; va_start(args,format);
    std::vsnprintf(line,sizeof(line),format,args); va_end(args);
    messages.emplace_back(line); SetLastError(0x76543210);
}
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
constexpr unsigned gate = 0xB551B7, accepted = 0xB551E0;
struct Signature { unsigned rva; const char* bytes; };
const Signature signatures[] = {
    {gate, "33 C0 8D 4F 28 8D 64 24 00 8B 11 85 D2 74 12 39 56 58 0F 84 47 01 00 00 40 83 C1 04 83 F8 08 7C E8 84 DB 0F 84 0C 04 00 00"},
    {0xB551AE, "8A 9F CF 00 00 00 80 E3 01"},
    {accepted, "F6 87 CC 00 00 00 40 74 12 F3 0F 10 46 50"},
    {0xB55316, "84 DB 0F 85 CE 02 00 00 E9 BD FE FF FF"},
    {0xB5DF6B, "8D 7E 28"}, {0xB5DFA9, "83 C7 04 88 41 02"},
    {0xB5E0D0, "83 39 00 74 0B 40 83 C1 04 83 F8 08 7C F2"}
};
void put(unsigned char* destination, const char* hex) {
    while (*hex) {
        while (*hex == ' ') ++hex;
        char* next{}; const auto value = std::strtoul(hex, &next, 16);
        if (next == hex) break;
        *destination++ = static_cast<unsigned char>(value); hex = next;
    }
}
unsigned fourcc(const char* value) { unsigned result{}; std::memcpy(&result, value, 4); return result; }
void set(unsigned char* data, unsigned offset, unsigned value) { std::memcpy(data + offset, &value, 4); }
unsigned beforeFlags{}, afterFlags{}, afterEcx{}, afterEdx{}, afterEsi{}, afterEdi{}, afterEbx{};
alignas(16) unsigned char originalFp[512]{}, beforeFp[512]{}, afterFp[512]{};
alignas(16) const unsigned simdSeed[] = {0x12345678, 0x89ABCDEF, 0x456789AB, 0xFEDCBA98};
unsigned invoke(void* address, const void* data, const void* message) {
    unsigned answer{};
    __asm {
        push ebx
        push esi
        push edi
        fxsave originalFp
        fninit
        fld1
        fldz
        fldpi
        movdqu xmm0, simdSeed
        movdqu xmm1, simdSeed
        movdqu xmm2, simdSeed
        movdqu xmm3, simdSeed
        movdqu xmm4, simdSeed
        movdqu xmm5, simdSeed
        movdqu xmm6, simdSeed
        movdqu xmm7, simdSeed
        fxsave beforeFp
        mov edi, data
        mov esi, message
        xor ebx, ebx
        mov bl, byte ptr [edi + 0CFh]
        and bl, 1
        mov eax, 11223344h
        mov ecx, 55667788h
        mov edx, 1234ABCDh
        stc
        pushfd
        pop beforeFlags
        call dword ptr [address]
        mov answer, eax
        pushfd
        pop afterFlags
        mov afterEcx, ecx
        mov afterEdx, edx
        mov afterEsi, esi
        mov afterEdi, edi
        mov afterEbx, ebx
        fxsave afterFp
        fxrstor originalFp
        pop edi
        pop esi
        pop ebx
    }
    return answer;
}
bool protect(void* memory, DWORD mode) { DWORD old{}; return VirtualProtect(memory, 4096, mode, &old) != FALSE; }
}
int wmain(int argc, wchar_t** argv) {
    CHECK(MH_Initialize() == MH_OK);
    void* loggerOriginal{};
    void* logger=reinterpret_cast<void*>(&crabe::di1::log);
    CHECK(MH_CreateHook(logger,reinterpret_cast<void*>(&traceLog),&loggerOriginal)==MH_OK);
    CHECK(MH_EnableHook(logger)==MH_OK);
    // Exercise the optional diagnostic path and calls after its 96-event budget.
    // config() exposes a const view of the mutable settings owned by common.cpp.
    auto& options = const_cast<crabe::di1::Config&>(crabe::di1::config());
    const bool previousTrace = options.traceCharacterFreedom;
    options.traceCharacterFreedom = true;
    constexpr unsigned size = 0xB60000;
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image) return 1;
    std::memset(image, 0xCC, size);
    for (const auto& signature : signatures) put(image + signature.rva, signature.bytes);
    const auto baseline = std::vector<unsigned char>(image, image + size);
    for (const auto& signature : signatures) {
        image[signature.rva] ^= 1;
        CHECK(!crabe::di1::installCharacterTargets(reinterpret_cast<std::uintptr_t>(image)));
        image[signature.rva] ^= 1;
        CHECK(!std::memcmp(image, baseline.data(), size));
    }
    CHECK(crabe::di1::installCharacterTargets(reinterpret_cast<std::uintptr_t>(image)));
    CHECK(crabe::di1::installCharacterTargets(reinterpret_cast<std::uintptr_t>(image)));
    // Actual measured native gate/loop and branch displacements execute against
    // owned records. Only the native accept/reject continuation is a stand-in.
    put(image + accepted, "B8 01 00 00 00 C3");
    put(image + 0xB555EC, "31 C0 C3");
    auto* control=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if (!control) return 1;
    std::memcpy(control,baseline.data(),size);
    put(control+accepted,"B8 01 00 00 00 C3"); put(control+0xB555EC,"31 C0 C3");
    auto* data = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    auto* message = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!data || !message) return 1;
    const auto reset = [&] {
        CHECK(protect(data, PAGE_READWRITE)); CHECK(protect(message, PAGE_READWRITE));
        std::memset(data, 0, 4096); std::memset(message, 0xA7, 4096);
        set(data, 0x28, fourcc("pstl")); set(data, 0x2C, fourcc("toma")); set(data, 0x30, fourcc("lctg"));
        data[0xCC] = 0x80;
    };
    for (const char* tag : {"apxb", "bpxb", "blas", "prht", "mksp", "frpn", "snow", "bbgn", "vprj", "vpdr", "chrr", "chrv", "flnt", "pmpk"}) {
        reset(); set(message, 0x58, fourcc(tag));
        std::array<unsigned char, 4096> savedData{}, savedMessage{};
        std::memcpy(savedData.data(), data, 4096); std::memcpy(savedMessage.data(), message, 4096);
        CHECK(protect(data, PAGE_READONLY)); CHECK(protect(message, PAGE_READONLY));
        // Test more calls than any diagnostic budget could hide.
        for (unsigned iteration = 0; iteration != 130; ++iteration) {
            SetLastError(0xBAD01234);
            CHECK(invoke(image + gate, data, message) == 1);
            CHECK(GetLastError() == 0xBAD01234);
            CHECK(beforeFlags == afterFlags);
            CHECK(!std::memcmp(beforeFp, afterFp, sizeof(beforeFp)));
            CHECK(afterEcx == 0x55667788 && afterEdx == 0x1234ABCD && afterEbx == 0);
            CHECK(afterEsi == reinterpret_cast<unsigned>(message) && afterEdi == reinterpret_cast<unsigned>(data));
        }
        CHECK(!std::memcmp(savedData.data(), data, 4096)); CHECK(!std::memcmp(savedMessage.data(), message, 4096));
    }
    CHECK(messages.size()==97 && messages.back().find("marked target impact budget exhausted")!=std::string::npos);
    messages.clear();
    // Unmarked crate traces have a separate budget and never alter the native
    // inclusion/exclusion truth table, registers, flags, FP state or records.
    for (const bool ignore : {false,true}) for (const bool listed : {false,true}) {
        reset(); set(data,0x28,fourcc("blas")); set(data,0x2C,0); data[0xCF]=ignore;
        set(message,0x58,fourcc(listed ? "blas":"xxxx"));
        set(message,0x04,0x12340001); set(message,0x38,0x12340002);
        const float damage=1.5f; std::memcpy(message+0x50,&damage,sizeof(damage));
        std::array<unsigned char,4096> savedData{},savedMessage{};
        std::memcpy(savedData.data(),data,4096); std::memcpy(savedMessage.data(),message,4096);
        CHECK(protect(data,PAGE_READONLY) && protect(message,PAGE_READONLY));
        const auto expected=invoke(control+gate,data,message);
        const auto flags=afterFlags,ecx=afterEcx,edx=afterEdx,ebx=afterEbx;
        std::array<unsigned char,512> fp{}; std::memcpy(fp.data(),afterFp,512);
        SetLastError(0xBAD01234);
        CHECK(invoke(image+gate,data,message)==expected && expected==unsigned(listed!=ignore));
        CHECK(GetLastError()==0xBAD01234 && afterFlags==flags && afterEcx==ecx && afterEdx==edx && afterEbx==ebx);
        CHECK(!std::memcmp(fp.data(),afterFp,512) && afterEsi==reinterpret_cast<unsigned>(message) && afterEdi==reinterpret_cast<unsigned>(data));
        CHECK(!std::memcmp(savedData.data(),data,4096) && !std::memcmp(savedMessage.data(),message,4096));
        CHECK(messages.back().find("unmarked Exploder impact")!=std::string::npos);
        CHECK(messages.back().find("target=12340002 owner=12340001")!=std::string::npos);
        CHECK(messages.back().find(listed!=ignore ? "nativeListAccept=1":"nativeListAccept=0")!=std::string::npos);
    }
    for (unsigned i=0; i<130; ++i) {
        SetLastError(0xBAD01234); CHECK(invoke(image+gate,data,message)==0 && GetLastError()==0xBAD01234);
    }
    CHECK(messages.size()==97 && messages.back().find("unmarked impact traces suppressed")!=std::string::npos);
    // Optional diagnostic fields may be inaccessible even when the measured
    // native gate can read its weapon field. Failed observation must stay passive.
    crabe::di1::removeCharacterTargets();
    std::memcpy(image+accepted,baseline.data()+accepted,6);
    CHECK(crabe::di1::installCharacterTargets(reinterpret_cast<std::uintptr_t>(image)));
    put(image+accepted,"B8 01 00 00 00 C3");
    auto* boundary=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));
    CHECK(boundary!=nullptr);
    if (boundary) {
        auto* shortMessage=boundary+4096-0x58;
        set(shortMessage,0x58,fourcc("blas")); CHECK(protect(boundary,PAGE_NOACCESS));
        reset(); set(data,0x28,fourcc("blas")); set(data,0x2C,0);
        const auto before=messages.size(); SetLastError(0xBAD01234);
        CHECK(invoke(image+gate,data,shortMessage)==1 && GetLastError()==0xBAD01234);
        CHECK(messages.size()==before);
        VirtualFree(boundary,0,MEM_RELEASE);
    }
    for (const char* tag : {"bbls", "lrcb", "lrgb", "xxxx"}) {
        reset(); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 0);
    }
    for (const char* tag : {"pstl", "toma"}) {
        reset(); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 1);
    }
    reset(); set(data, 0x30, 0); set(message, 0x58, fourcc("blas")); CHECK(invoke(image + gate, data, message) == 0);
    reset(); set(data, 0x28, fourcc("lrcb")); set(message, 0x58, fourcc("blas")); CHECK(invoke(image + gate, data, message) == 0);
    reset(); data[0xCF] = 1; set(data, 0x34, fourcc("blas")); set(message, 0x58, fourcc("blas")); CHECK(invoke(image + gate, data, message) == 0);
    reset(); data[0xCC] = 0; set(message, 0x58, fourcc("blas")); CHECK(invoke(image + gate, data, message) == 0);
    // Scripted personal projectiles obey the same narrow target opt-in as native
    // weapon-table projectiles; no change to unmarked, cannon or Gatling targets.
    for (const char* tag : {"flnt", "pmpk"}) {
        reset(); set(data, 0x30, 0); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 0);
        reset(); set(data, 0x28, fourcc("lrcb")); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 0);
        reset(); set(data, 0x2C, fourcc("lrgb")); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 0);
        reset(); data[0xCF] = 1; set(data, 0x34, fourcc(tag)); set(message, 0x58, fourcc(tag)); CHECK(invoke(image + gate, data, message) == 0);
    }
    // All eight native slots still work; the extension never reads a ninth slot.
    reset(); for (unsigned index = 2; index != 8; ++index) set(data, 0x28 + 4 * index, 0x11110000 + index);
    set(message, 0x58, 0x11110007); CHECK(invoke(image + gate, data, message) == 1);
    crabe::di1::removeCharacterTargets();
    std::memcpy(image + accepted, baseline.data() + accepted, 6);
    std::memcpy(image + 0xB555EC, baseline.data() + 0xB555EC, 3);
    CHECK(!std::memcmp(image, baseline.data(), size));
    VirtualFree(data, 0, MEM_RELEASE); VirtualFree(message, 0, MEM_RELEASE); VirtualFree(image, 0, MEM_RELEASE);
    VirtualFree(control,0,MEM_RELEASE);
    if (argc > 1) {
        HMODULE original = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(original != nullptr);
        if (original) {
            auto* base = reinterpret_cast<unsigned char*>(original);
            std::array<unsigned char, 64> saved{}; std::memcpy(saved.data(), base + gate, saved.size());
            CHECK(crabe::di1::installCharacterTargets(reinterpret_cast<std::uintptr_t>(base)));
            crabe::di1::removeCharacterTargets();
            CHECK(!std::memcmp(saved.data(), base + gate, saved.size()));
            FreeLibrary(original);
        }
    }
    CHECK(MH_DisableHook(logger)==MH_OK && MH_RemoveHook(logger)==MH_OK);
    CHECK(MH_Uninitialize() == MH_OK);
    options.traceCharacterFreedom = previousTrace;
    std::printf("Native target gate/ABI/read-only/rollback: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
