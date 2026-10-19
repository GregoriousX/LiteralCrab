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
void __fastcall completed(void* status, void*) {
    CHECK(status == expectedStatus); ++signals;
    DWORD old{};
    CHECK(VirtualProtect(retiredName, 4096, PAGE_NOACCESS, &old));
}
void* __cdecl allocate(unsigned size, bool, unsigned*, void*) {
    return VirtualAlloc(nullptr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}
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
bool safeAlloc(Alloc function, void** output, void* status) {
    __try { return function(reinterpret_cast<const char*>(retiredName), output, 2, false, nullptr, status); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    CHECK(MH_Uninitialize() == MH_OK);
    VirtualFree(retiredName, 0, MEM_RELEASE); VirtualFree(image, 0, MEM_RELEASE);
    std::printf("Asynchronous filename retirement: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
