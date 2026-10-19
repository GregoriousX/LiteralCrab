// SPDX-License-Identifier: GPL-3.0-only
// Invoke the actual adapter with native x86 stand-ins. Optional EXE argument
// validates the real profile on a private mapping; never starts or writes the game.
#include "di1/adapters.hpp"
#include <windows.h>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string_view>
#include <vector>

struct lua_State { int kind = 3; double argument = 3000063, answer{}; unsigned pushed{}; };
namespace {
int failures{};
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
constexpr unsigned findRva = 0x7E620, categoryRva = 0x85A80, gatewayLoad = 0x833E75;
unsigned categoryCalls{}, findCalls{}, writes{};
unsigned observedLow{}, observedHigh{};
void* observedReader{};
int category = 1;
bool faultCategory{}, faultFind{}, clearGateway{};
void** gatewaySlot{};
void* found{};
std::array<unsigned, 8> tag{};
int __fastcall classify(void* reader, void*, unsigned id) {
    observedReader = reader; observedLow = id; ++categoryCalls;
    if (faultCategory) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    if (clearGateway) *gatewaySlot = nullptr;
    return category;
}
void* __fastcall find(void* reader, void*, unsigned low, unsigned high) {
    observedReader = reader; observedLow = low; observedHigh = high; ++findCalls;
    if (faultFind) RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
    return found;
}
void __fastcall add(void*, void*, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned) { ++writes; }
void __fastcall captureRemove(void*, void*, unsigned, unsigned) { ++writes; }
double __cdecl number(lua_State* s, int) { return s->argument; }
int __cdecl boolean(lua_State*, int) { return 0; }
int __cdecl type(lua_State* s, int) { return s->kind; }
const char* __cdecl string(lua_State*, int, std::size_t*) { return nullptr; }
void __cdecl pushBoolean(lua_State*, int) {}
void __cdecl pushNumber(lua_State* s, double n) { s->answer = n; ++s->pushed; }
bool write(void* dest, const void* data, std::size_t size) {
    DWORD old{};
    if (!VirtualProtect(dest, size, PAGE_EXECUTE_READWRITE, &old)) return false;
    std::memcpy(dest, data, size);
    DWORD ignored{};
    const bool ok = VirtualProtect(dest, size, old, &ignored) != FALSE;
    FlushInstructionCache(GetCurrentProcess(), dest, size);
    return ok;
}
void pattern(unsigned char* base, unsigned rva, const char* text) {
    unsigned byte{};
    for (; *text; ++rva) {
        if (*text == '?') byte = 0;
        else std::sscanf(text, "%2x", &byte);
        base[rva] = static_cast<unsigned char>(byte);
        text += 2;
        if (*text == ' ') ++text;
    }
}
void call(unsigned char* base, unsigned site, unsigned target) {
    base[site] = 0xE8;
    const auto rel = static_cast<int>(target - site - 5);
    std::memcpy(base + site + 1, &rel, 4);
}
void jump(unsigned char* dest, const void* function) {
    unsigned char bytes[] = {0xE9, 0, 0, 0, 0};
    const auto rel = static_cast<int>(reinterpret_cast<std::uintptr_t>(function) - reinterpret_cast<std::uintptr_t>(dest + 5));
    std::memcpy(bytes + 1, &rel, 4);
    CHECK(write(dest, bytes, sizeof(bytes)));
}
crabe::mods::LuaCFunction lookup(const std::vector<crabe::mods::LibraryFunction>& api, std::string_view name) {
    for (const auto& item : api) if (item.name == name) return item.function;
    return nullptr;
}
void fixture(unsigned char* base) {
    pattern(base, 0x877B0, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 3C 56 8B F1");
    pattern(base, 0x827E0, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 34 53 55 8B 6C 24 4C");
    pattern(base, findRva, "53 56 8B 74 24 0C 57 8B 7C 24 14 33 D2");
    pattern(base, categoryRva, "55 8B EC 83 E4 F8 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 48 53 8B D9");
    pattern(base, gatewayLoad, "8B 0D ?? ?? ?? ?? E8");
    void* slot = base + 0x1300000;
    std::memcpy(base + gatewayLoad + 2, &slot, sizeof(slot));
    pattern(base, 0x7E370, "8B 44 24 04 80 89 5C 01 00 00 02 8B 54 24 08 89 41 08 8B 44 24 10 89 41 14 89 51 0C");
    pattern(base, 0x7E630, "52 E8 CA FF FF FF 39 70 08 75 05 39 78 0C 74 08 42 83 FA 0C 72 EA 33 C0 5F 5E 5B C2 08 00");
    call(base, 0x833EBF, 0x877B0); call(base, 0x833E7B, 0x827E0);
    call(base, 0x82806, findRva); call(base, 0x87832, categoryRva); call(base, 0x87826, 0x7E370);
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace crabe;
    HMODULE host = argc > 1 ? LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES) : nullptr;
    auto* base = argc > 1 ? reinterpret_cast<unsigned char*>(host) : static_cast<unsigned char*>(
        VirtualAlloc(nullptr, 0x1400000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    CHECK(base != nullptr);
    if (!base) return 1;
    if (argc == 1) { fixture(base); host = reinterpret_cast<HMODULE>(base); }
    mods::LuaApi lua;
    lua.type = type; lua.toNumber = number; lua.toBoolean = boolean; lua.toLString = string;
    lua.pushBoolean = pushBoolean; lua.pushNumber = pushNumber;
    const auto originalCode = std::vector<unsigned char>(base + 0x7E000, base + 0x88000);
    const auto resolve = [&] { return di1::resolveAdapters(host, lua); };
    auto adapters = resolve();
    CHECK(lookup(adapters, "GetDiscOwner"));
    CHECK(std::memcmp(base + 0x7E000, originalCode.data(), originalCode.size()) == 0);
    // Only the new capability is withheld by an unknown record layout/call.
    for (unsigned rva : {0x7E370u, 0x7E630u, 0x87827u}) {
        const auto byte = base[rva]; const auto changed = static_cast<unsigned char>(byte ^ 1);
        CHECK(write(base + rva, &changed, 1));
        const auto partial = resolve();
        CHECK(!lookup(partial, "GetDiscOwner"));
        for (const auto* name : {"PlaceDisc", "RemoveDisc", "IsDiscPlaced", "TagCategory"}) CHECK(lookup(partial, name));
        CHECK(base[rva] == changed); // Resolver never patches the executable.
        CHECK(write(base + rva, &byte, 1));
    }
    lua.pushNumber = nullptr;
    adapters = resolve(); CHECK(!lookup(adapters, "GetDiscOwner"));
    CHECK(lookup(adapters, "PlaceDisc") && lookup(adapters, "IsDiscPlaced"));
    lua.pushNumber = pushNumber;
    adapters = resolve();
    const auto owner = lookup(adapters, "GetDiscOwner");
    CHECK(owner != nullptr);
    if (!owner) { if (argc > 1) FreeLibrary(host); else VirtualFree(base, 0, MEM_RELEASE); return 1; }
    gatewaySlot = *reinterpret_cast<void***>(base + gatewayLoad + 2);
    void* oldReader = *gatewaySlot;
    std::array<unsigned, 16> reader{};
    *gatewaySlot = reader.data();
    jump(base + categoryRva, reinterpret_cast<void*>(&classify));
    jump(base + findRva, reinterpret_cast<void*>(&find));
    jump(base + 0x877B0, reinterpret_cast<void*>(&add));
    jump(base + 0x827E0, reinterpret_cast<void*>(&captureRemove));
    lua_State state;
    const auto query = [&](double id, int expected, int kind = 3) {
        state.argument = id; state.kind = kind; state.answer = 999; state.pushed = 0;
        const auto before = tag;
        CHECK(owner(&state) == 1 && state.pushed == 1 && state.answer == expected);
        CHECK(before == tag && writes == 0);
    };
    tag[2] = 3000063; tag[3] = 0; tag[4] = 9; tag[5] = 0;
    found = tag.data();
    query(3000063, 0); CHECK(observedReader == reader.data() && observedLow == 3000063 && observedHigh == 0);
    tag[5] = 1; query(3000063, 1);
    // Position +0x10 is a type, never an owner. Requery current native state.
    tag[4] = 0; tag[5] = 1; query(3000063, 1);
    tag[4] = 9; tag[5] = 0; query(3000063, 0);
    for (unsigned antenna : {2u, 9u, ~0u}) { tag[5] = antenna; query(3000063, -2); }
    tag[5] = 0; tag[2] = 3000062; query(3000063, -2);
    tag[2] = 3000063; tag[3] = 1; query(3000063, -2); tag[3] = 0;
    found = nullptr; query(3000063, -1);
    found = tag.data();
    const auto beforeInvalid = findCalls;
    for (double invalid : {0.0, -1.0, 3000063.5, 100000000.0, std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) query(invalid, -2);
    for (int kind : {-1, 0, 1, 4, 5}) query(3000063, -2, kind);
    CHECK(findCalls == beforeInvalid);
    for (int rejected : {-1, 0, 2, 3, 4}) { category = rejected; query(4000018, -2); }
    category = 1;
    *gatewaySlot = nullptr; query(3000063, -2); *gatewaySlot = reader.data();
    clearGateway = true; query(3000063, -2); clearGateway = false; *gatewaySlot = reader.data();
    faultCategory = true; query(3000063, -2); faultCategory = false;
    faultFind = true; query(3000063, -2); faultFind = false;
    void* inaccessible = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_NOACCESS);
    CHECK(inaccessible != nullptr);
    found = inaccessible; query(3000063, -2);
    VirtualFree(inaccessible, 0, MEM_RELEASE);
    found = tag.data(); query(3000063, 0);
    *gatewaySlot = oldReader;
    CHECK(write(base + 0x7E000, originalCode.data(), originalCode.size()));
    CHECK(std::memcmp(base + 0x7E000, originalCode.data(), originalCode.size()) == 0);
    if (argc > 1) FreeLibrary(host); else CHECK(VirtualFree(base, 0, MEM_RELEASE));
    std::printf("Disc ownership %s arguments/native ABI/read-only/error/profile checks: %s\n", argc > 1 ? "original executable" : "fixture", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
