// SPDX-License-Identifier: GPL-3.0-only
#include "di1/asset_loader.hpp"
#include "minhook/MinHook.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace {
int failures{};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
unsigned char* retiredName{}; unsigned signals{}; void* expectedStatus{};
unsigned allocationCalls{}, foreignCalls{};
bool signalThrows{};
constexpr DWORD completionException=0xE04C4353;
void __fastcall completed(void* status, void*) {
    CHECK(status == expectedStatus); ++signals;
    DWORD old{};
    CHECK(VirtualProtect(retiredName, 4096, PAGE_NOACCESS, &old));
    if (signalThrows) RaiseException(completionException,0,0,nullptr);
}
void* __cdecl allocate(unsigned size, bool, unsigned*, void*) {
    ++allocationCalls;
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
bool __cdecl foreignLoad(const char*, void*, unsigned, unsigned, void*) { ++foreignCalls; return false; }
bool __cdecl foreignAlloc(const char*, void**, unsigned, bool, void*, void*) { ++foreignCalls; return false; }
bool provider(const char* name, std::string& data) { CHECK(!std::strcmp(name, "new/path/custom.dnax")); data = "abcdef"; return true; }
void put(unsigned char* target, const char* hex) {
    while (*hex) {
        while (*hex == ' ') ++hex;
        char* next{}; const auto value = std::strtoul(hex, &next, 16); if (next == hex) break;
        *target++ = static_cast<unsigned char>(value); hex = next;
    }
}
void branch(unsigned char* image, unsigned site, unsigned target) {
    image[site] = 0xE8;
    *reinterpret_cast<int*>(image + site + 1) = static_cast<int>(target) - static_cast<int>(site + 5);
}
void jump(unsigned char* site, const void* destination) {
    site[0] = 0xE9;
    *reinterpret_cast<int*>(site + 1) = static_cast<int>(reinterpret_cast<std::uintptr_t>(destination) - reinterpret_cast<std::uintptr_t>(site + 5));
    FlushInstructionCache(GetCurrentProcess(), site, 5);
}
using Load = bool(__cdecl*)(const char*, void*, unsigned, unsigned, void*);
using Alloc = bool(__cdecl*)(const char*, void**, unsigned, bool, void*, void*);
bool safeLoad(Load function, void* destination, void* status) {
    __try { return function(reinterpret_cast<const char*>(retiredName), destination, 3, 1, status); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool safeAlloc(Alloc function, void** output, void* status, unsigned extra=2) {
    __try { return function(reinterpret_cast<const char*>(retiredName), output, extra, false, nullptr, status); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
DWORD completionLoad(Load function,void* destination,void* status) {
    __try { function(reinterpret_cast<const char*>(retiredName),destination,3,1,status); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    return 0;
}
DWORD completionAlloc(Alloc function,void** output,void* status) {
    __try { function(reinterpret_cast<const char*>(retiredName),output,2,false,nullptr,status); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return GetExceptionCode(); }
    return 0;
}
}
int main() {
    CHECK(MH_Initialize() == MH_OK);
    auto* image = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x20000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!image) return 1;
    std::memset(image, 0xCC, 0x20000);
    put(image + 0x1E200, "8B 44 24 04 81 EC 84 00 00 00 57 50 8D 4C 24 0C 68 80 00 00 00 51 E8 85 F5 FF FF");
    put(image + 0x1E5F0, "8B 44 24 04 81 EC 84 00 00 00 56 50 8D 4C 24 0C 68 80 00 00 00 51 E8 95 F1 FF FF");
    put(image + 0x1D5F0, "8B 01 50 FF 15");
    put(image + 0xA3B0, "8B 44 24 0C 8B 4C 24 04 85 C0 74 02 89 08");
    for (const auto [site, target] : {std::pair{0x1E28Fu,0x1D5F0u}, {0x1E69Cu,0x1D5F0u}, {0x1E662u,0xA3B0u}, {0x1EC86u,0x1E5F0u}, {0x1EBD0u,0x1E200u}})
        branch(image, site, target);
    // A disabled foreign hook leaves native profile bytes intact. Both early
    // and partial failure must leave that foreign hook owned and usable.
    for (const bool conflictOnAlloc : {false,true}) {
        void* foreignOriginal{};
        void* target=image+(conflictOnAlloc ? 0x1E5F0:0x1E200);
        void* callback=conflictOnAlloc ? reinterpret_cast<void*>(&foreignAlloc):reinterpret_cast<void*>(&foreignLoad);
        CHECK(MH_CreateHook(target,callback,&foreignOriginal)==MH_OK);
        CHECK(!crabe::di1::installAssetLoader(reinterpret_cast<HMODULE>(image),provider,true));
        CHECK(MH_RemoveHook(image+(conflictOnAlloc ? 0x1E200:0x1E5F0))==MH_ERROR_NOT_CREATED);
        const auto enabled=MH_EnableHook(target);
        CHECK(enabled==MH_OK);
        // Report a removed-foreign-hook regression without executing the raw
        // synthetic native body, whose unused continuation is deliberately CC.
        if (enabled!=MH_OK) { MH_Uninitialize(); VirtualFree(image,0,MEM_RELEASE); return 1; }
        if (conflictOnAlloc) CHECK(!reinterpret_cast<Alloc>(target)(nullptr,nullptr,0,false,nullptr,nullptr));
        else CHECK(!reinterpret_cast<Load>(target)(nullptr,nullptr,0,0,nullptr));
        // An active foreign hook must also be refused without being removed.
        CHECK(!crabe::di1::installAssetLoader(reinterpret_cast<HMODULE>(image),provider,true));
        CHECK(MH_DisableHook(target)==MH_OK && MH_RemoveHook(target)==MH_OK);
    }
    CHECK(foreignCalls==2);
    CHECK(crabe::di1::installAssetLoader(reinterpret_cast<HMODULE>(image), provider, true));
    jump(image + 0x1D5F0, reinterpret_cast<const void*>(&completed));
    jump(image + 0xA3B0, reinterpret_cast<const void*>(&allocate));
    retiredName = static_cast<unsigned char*>(VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    if (!retiredName) return 1;
    for (bool allocated : {false, true}) {
        DWORD old{}; CHECK(VirtualProtect(retiredName, 4096, PAGE_READWRITE, &old));
        std::strcpy(reinterpret_cast<char*>(retiredName), "new/path/custom.dnax");
        std::array<unsigned char, 0x20> status{}; expectedStatus = status.data();
        char destination[4]{}; void* output{};
        if (allocated) {
            CHECK(safeAlloc(reinterpret_cast<Alloc>(image + 0x1E5F0), &output, status.data()));
            CHECK(output && !std::memcmp(output, "abcdef\0\0", 8));
            CHECK(*reinterpret_cast<void**>(status.data() + 0x18) == output);
            CHECK(*reinterpret_cast<unsigned*>(status.data() + 0x14) == 8);
            if (output) VirtualFree(output, 0, MEM_RELEASE);
        } else {
            CHECK(safeLoad(reinterpret_cast<Load>(image + 0x1E200), destination, status.data()));
            CHECK(!std::memcmp(destination, "bcd", 3));
            CHECK(*reinterpret_cast<void**>(status.data() + 0x18) == destination);
            CHECK(*reinterpret_cast<unsigned*>(status.data() + 0x14) == 3);
        }
        CHECK(*reinterpret_cast<unsigned*>(status.data() + 0x10) == 0);
    }
    CHECK(signals == 2);
    CHECK(allocationCalls==1);
    // Reject wraparound before entering the engine allocator. This measured
    // trampoline continuation restores its native stack and returns a cache miss.
    put(image+0x1E5F0+10,"81 C4 84 00 00 00 33 C0 C3");
    FlushInstructionCache(GetCurrentProcess(),image+0x1E5F0+10,9);
    DWORD old{}; CHECK(VirtualProtect(retiredName,4096,PAGE_READWRITE,&old));
    std::strcpy(reinterpret_cast<char*>(retiredName),"new/path/custom.dnax");
    std::array<unsigned char,0x20> rejectedStatus; rejectedStatus.fill(0xAB);
    const auto originalStatus=rejectedStatus;
    void* rejectedOutput=reinterpret_cast<void*>(0x12345678);
    for (unsigned extra : {~0u,~0u-5}) {
        CHECK(!safeAlloc(reinterpret_cast<Alloc>(image+0x1E5F0),&rejectedOutput,rejectedStatus.data(),extra));
        CHECK(allocationCalls==1 && signals==2 && rejectedOutput==reinterpret_cast<void*>(0x12345678));
        CHECK(rejectedStatus==originalStatus);
    }
    // After the completion signal starts, the request can already be retired.
    // Preserve a native signal exception instead of swallowing it and falling
    // through to a second load with inaccessible request/name memory.
    signalThrows=true;
    for (const bool allocated : {false,true}) {
        CHECK(VirtualProtect(retiredName,4096,PAGE_READWRITE,&old));
        std::strcpy(reinterpret_cast<char*>(retiredName),"new/path/custom.dnax");
        std::array<unsigned char,0x20> status{}; expectedStatus=status.data();
        char destination[4]{}; void* output{};
        const auto beforeSignals=signals;
        if (allocated) {
            CHECK(completionAlloc(reinterpret_cast<Alloc>(image+0x1E5F0),&output,status.data())==completionException);
            CHECK(output && !std::memcmp(output,"abcdef\0\0",8));
            if (output) VirtualFree(output,0,MEM_RELEASE);
        } else {
            CHECK(completionLoad(reinterpret_cast<Load>(image+0x1E200),destination,status.data())==completionException);
            CHECK(!std::memcmp(destination,"bcd",3));
        }
        CHECK(signals==beforeSignals+1);
    }
    CHECK(MH_Uninitialize() == MH_OK);
    VirtualFree(retiredName, 0, MEM_RELEASE); VirtualFree(image, 0, MEM_RELEASE);
    std::printf("Loader lifetime/native signal exceptions/foreign hooks/allocation bounds: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
