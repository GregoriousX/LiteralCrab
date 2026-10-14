// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_freedom_policy.hpp"
#include "di1/character_freedom.hpp"
#include "di1/character_particles.hpp"
#include "di1/particle_directory.hpp"
#include "minhook/MinHook.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <thread>
#include <fstream>
#include <filesystem>

namespace {
const char* testPlayset = "Incredibles";
const char* testRoster = "AV_MrIncredible,AV_Dash";
const char* testCurrentPlayset = "Pirates";
unsigned observedActor{}, observedAwards{};
float observedAmount{};
bool observedTracked{};
void* observedSelf{};
unsigned observedRules{}, observedModels{};
unsigned observedInitializations{};
unsigned observedControls{}, observedActions{}, observedControlValue{};
const char** observedContext{};
const char* observedGroup{};
const char* observedAction{};
unsigned observedDataActor{}, observedDataType{}, observedContextReads{};
unsigned char contextData[0x30]{};
int testPlayerIndex = 0, testWorldResult = 0;
bool testContextResult = false;
bool testBlockedUse{}, testBlockedSpectator{};
unsigned observedWorldCalls{}, observedGetCalls{};
unsigned observedAssetCalls{}, observedAssetType{};
const char* observedAssetName{};
const void* observedZones{};
unsigned observedUnlockedCalls{}, observedProjectileCalls{}, observedProjectileFlags{}, observedProjectileKind{};
const char* observedUnlockedName{};
bool testUnlockedResult{};
unsigned char* testRefreshEntry{};
unsigned observedRefreshCalls{};
bool testProgressionUnlocked = true, observedRefreshForced{};
int(__thiscall* testRefreshWorld)(void*, const void*){};
struct ProjectileVector { float x, y, z; };
ProjectileVector observedProjectilePosition{};
void* observedProjectileOwner{};
unsigned projectileResultActor[2] = {0, 0xCAFE1234};
unsigned observedEmitterCalls{}, observedEmitterId{};
unsigned char testFxSystem[0x110]{};
struct ParticleNode { unsigned padding[3]; const void* recipe; unsigned id; };
std::vector<ParticleNode> particleNodes[3];
std::vector<unsigned char> particleSource;
unsigned char particleWorld[0x1C]{}, particleSupplement[0x1C]{};
unsigned observedParticleLoads{}, observedParticleImports{}, observedParticleUnloads{};
void* observedParticleMaterials{};
void* observedParticleReleased[8]{};
bool testParticleImportResult = true, testParticleLoadFail{}, testParticleCached{};
bool(__cdecl* nativeParticleImport)(void*, unsigned, void*){};
std::vector<unsigned char> particleFixture() {
    std::vector<unsigned char> data(196, 0);
    for (unsigned i = 0; i < 80; ++i) data[i] = static_cast<unsigned char>(i);
    unsigned position = 80;
    for (unsigned kind = 0; kind < 3; ++kind)
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned offset = i * 16 + kind * 4, id = 10 + kind * 10 + i;
            std::memcpy(data.data() + position, &offset, 4);
            std::memcpy(data.data() + position + 4, &id, 4);
            position += static_cast<unsigned>(crabe::di1::ParticleDirectory::stride[kind]);
        }
    const unsigned short format[] = {0x123, 12};
    const unsigned offsets[] = {64, 64};
    const unsigned short counts[] = {2, 3, 3, 3};
    std::memcpy(data.data() + position, format, 4);
    std::memcpy(data.data() + position + 4, offsets, 8);
    std::memcpy(data.data() + position + 12, counts, 8);
    return data;
}
std::vector<unsigned char> particleImportFixture() {
    std::vector<unsigned char> data(1356, 0);
    unsigned position = 1240;
    for (unsigned kind = 0; kind < 3; ++kind)
        for (unsigned i = 0; i < 3; ++i) {
            const unsigned offset = kind == 0 ? i * 272 : kind == 1 ? 1140 + i * 8 : 1100 + i * 12;
            const unsigned id = 10 + kind * 10 + i;
            std::memcpy(data.data() + position, &offset, 4);
            std::memcpy(data.data() + position + 4, &id, 4);
            if (kind == 0) {
                data[offset + 0x105] = 2;
                const unsigned short refs[] = {1, 4};
                std::memcpy(data.data() + offset + 0x108, refs, 4);
            } else if (kind == 2) {
                const unsigned short refs[] = {0xFFFF, 1, 4, 0xFFFF, 0xFFFF, 4};
                std::memcpy(data.data() + offset, refs, 12);
            }
            position += static_cast<unsigned>(crabe::di1::ParticleDirectory::stride[kind]);
        }
    for (unsigned i = 0; i < 40; ++i) data[1200 + i] = static_cast<unsigned char>(i);
    const unsigned short format[] = {0x123, 12};
    const unsigned offsets[] = {1200, 1200};
    const unsigned short counts[] = {5, 3, 3, 3};
    std::memcpy(data.data() + position, format, 4);
    std::memcpy(data.data() + position + 4, offsets, 8);
    std::memcpy(data.data() + position + 12, counts, 8);
    return data;
}
void* __fastcall captureParticleFind(void* map, void*, unsigned id) {
    constexpr unsigned offsets[] = {0x10, 0x4C, 0x88};
    for (unsigned kind = 0; kind < 3; ++kind) {
        if (map != testFxSystem + 0x18 + offsets[kind]) continue;
        for (auto& node : particleNodes[kind]) if (node.id == id) return &node;
    }
    return nullptr;
}
bool __cdecl captureParticleImport(void* data, unsigned size, void* materials) {
    ++observedParticleImports; observedParticleMaterials = materials;
    crabe::di1::ParticleDirectory view;
    if (!view.parse(data, size)) std::abort();
    if (!testParticleImportResult) return false;
    for (unsigned kind = 0; kind < 3; ++kind)
        for (unsigned i = 0; i < view.count[kind]; ++i) {
            const auto* row = view.row(kind, i);
            const unsigned offset = view.read<unsigned>(row), id = view.read<unsigned>(row + 4);
            auto* node = static_cast<ParticleNode*>(captureParticleFind(testFxSystem + 0x18 +
                (kind == 0 ? 0x10 : kind == 1 ? 0x4C : 0x88), nullptr, id));
            if (node) node->recipe = static_cast<unsigned char*>(data) + offset;
            else particleNodes[kind].push_back({{}, static_cast<unsigned char*>(data) + offset, id});
        }
    return true;
}
void* __cdecl captureParticleLoad(const char* name, unsigned flags) {
    ++observedParticleLoads;
    if (flags) std::abort();
    if (std::strcmp(name, "particles/rumpusroom")) return particleWorld;
    if (testParticleLoadFail) return nullptr;
    if (!testParticleCached && !nativeParticleImport(particleSource.data(), static_cast<unsigned>(particleSource.size()), particleWorld)) return nullptr;
    *reinterpret_cast<void**>(particleSupplement + 0x0C) = particleSource.data();
    *reinterpret_cast<void**>(particleSupplement + 0x10) = particleSource.data() + particleSource.size();
    return particleSupplement;
}
void __cdecl captureParticleUnload(void* data) {
    if (observedParticleUnloads >= 8) std::abort();
    observedParticleReleased[observedParticleUnloads++] = data;
}
void* __fastcall captureEmitter(void* self, void*, unsigned id) {
    observedSelf = self; observedEmitterId = id; ++observedEmitterCalls;
    return id & 1 ? nullptr : projectileResultActor;
}
bool __fastcall captureUnlocked(void* self, void*, const char* name) {
    observedSelf = self; observedUnlockedName = name; ++observedUnlockedCalls;
    return testRefreshEntry ? bool(testRefreshEntry[0x78] & 4) : testUnlockedResult;
}
void __fastcall captureRefresh(void* self, void*, bool forced) {
    ++observedRefreshCalls; observedRefreshForced = forced;
    if (self != testRefreshEntry) std::abort();
    // Stand in for native GetTool(false): progression precedes the zone check,
    // whose input already has bit2 set. Never synthesize an award or suppression.
    auto* entry = static_cast<unsigned char*>(self);
    if (!testProgressionUnlocked) return;
    entry[0x78] |= 4;
    if (!testRefreshWorld(self, *reinterpret_cast<void**>(entry + 0x24))) entry[0x78] &= ~4;
}
void* __cdecl captureProjectile(unsigned flags, ProjectileVector position, unsigned kind, void* owner) {
    ++observedProjectileCalls; observedProjectileFlags = flags; observedProjectilePosition = position;
    observedProjectileKind = kind; observedProjectileOwner = owner;
    return flags & 1 ? nullptr : projectileResultActor;
}
bool __fastcall captureAsset(void* self, void*, const char* name, unsigned type) {
    observedSelf = self; observedAssetName = name; observedAssetType = type; ++observedAssetCalls;
    return bool(type & 1);
}
int __fastcall capturePlayer(void*, void*, unsigned actor) { observedActor = actor; return testPlayerIndex; }
int __fastcall captureWorld(void* self, void*, const void* zones) {
    observedSelf = self; observedZones = zones; ++observedWorldCalls; return testWorldResult;
}
bool __fastcall captureMenuGet(void* self, void*, const char** context) {
    if (!std::strcmp(*context, "TUT_DISABLE_USE_ITEM")) return testBlockedUse;
    if (!std::strcmp(*context, "SPECTATOR_DISABLE")) return testBlockedSpectator;
    observedSelf = self; observedContext = context; ++observedGetCalls; return testContextResult;
}
void* __cdecl captureData(unsigned actor, unsigned type) {
    observedDataActor = actor; observedDataType = type; return contextData;
}
bool __fastcall captureContextGet(void* self, void*, const char** name) {
    if (self != contextData || !name || !*name) std::abort();
    ++observedContextReads;
    return !std::strcmp(*name, "Allow_UseItems");
}
void* observedRuleTools{};
const char* observedModel{};
unsigned observedFirst{}, observedSecond{};
void __fastcall captureRule(void* self, void*, void* tools) {
    observedSelf = self; observedRuleTools = tools; ++observedRules;
}
bool __fastcall captureModel(void* self, void*, const char* model, unsigned first, unsigned second) {
    observedSelf = self; observedModel = model; observedFirst = first; observedSecond = second;
    ++observedModels; return first != second;
}
void __fastcall captureInitialize(void* self, void*) { observedSelf = self; ++observedInitializations; }
void __fastcall captureControl(void* self, void*, const char** context, unsigned value) {
    observedSelf = self; observedContext = context; observedControlValue = value; ++observedControls;
}
void __fastcall captureAction(void* self, void*, const char* group, const char* action, unsigned first, unsigned second) {
    observedSelf = self; observedGroup = group; observedAction = action;
    observedFirst = first; observedSecond = second; ++observedActions;
}
void __fastcall captureAward(void* self, void*, unsigned actor, float amount, bool tracked) {
    observedSelf = self;
    observedActor = actor;
    observedAmount = amount;
    observedTracked = tracked;
    ++observedAwards;
}
const char* __cdecl testLuaString(lua_State*, int index, std::size_t* length) {
    const auto* value = index == 1 ? testPlayset : testRoster;
    *length = std::strlen(value);
    return value;
}
bool replaceCode(void* address, const void* data, std::size_t length) {
    DWORD protection{};
    if (!VirtualProtect(address, length, PAGE_EXECUTE_READWRITE, &protection)) return false;
    std::memcpy(address, data, length);
    const auto restored = VirtualProtect(address, length, protection, &protection);
    FlushInstructionCache(GetCurrentProcess(), address, length);
    return restored != FALSE;
}
}

int wmain(int argc, wchar_t** argv) {
    using crabe::di1::CharacterPlaysets;
    int failures = 0;
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
    // Every combination of retained directory rows, including empty/identity,
    // keeps recipes/envelopes and row order intact with no temporary allocation.
    const auto originalParticles = particleFixture();
    for (unsigned mask = 0; mask < 512; ++mask) {
        auto filtered = originalParticles;
        CHECK(crabe::di1::filterParticleDirectory(filtered.data(), filtered.size(),
            [mask](unsigned kind, unsigned id, unsigned) { return bool(mask & (1u << (kind * 3 + id % 10))); }));
        crabe::di1::ParticleDirectory view;
        CHECK(view.parse(filtered.data(), filtered.size()));
        CHECK(!std::memcmp(filtered.data(), originalParticles.data(), 80));
        unsigned position = 80;
        for (unsigned kind = 0; kind < 3; ++kind) {
            unsigned retained = 0;
            for (unsigned i = 0; i < 3; ++i) {
                if (mask & (1u << (kind * 3 + i))) {
                    CHECK(!std::memcmp(view.row(kind, retained), originalParticles.data() + position,
                        crabe::di1::ParticleDirectory::stride[kind]));
                    ++retained;
                }
                position += static_cast<unsigned>(crabe::di1::ParticleDirectory::stride[kind]);
            }
            CHECK(view.count[kind] == retained);
        }
        if (mask == 511) CHECK(filtered == originalParticles);
    }
    for (unsigned offset : {176u, 178u, 180u, 184u, 188u, 190u, 192u, 194u, 80u}) {
        auto malformed = originalParticles;
        malformed[offset] = 0xFF; malformed[offset + 1] = 0xFF;
        const auto before = malformed;
        CHECK(!crabe::di1::filterParticleDirectory(malformed.data(), malformed.size(), [](unsigned, unsigned, unsigned) { return false; }));
        CHECK(malformed == before);
    }
    {
        auto compact = particleImportFixture();
        CHECK(crabe::di1::compactParticleEnvelopes(compact.data(), compact.size()));
        crabe::di1::ParticleDirectory view; CHECK(view.parse(compact.data(), compact.size()));
        CHECK(view.curves == 2);
        CHECK(view.read<unsigned short>(compact.data() + 0x108) == 0);
        CHECK(view.read<unsigned short>(compact.data() + 0x10A) == 1);
        const auto original = particleImportFixture();
        CHECK(!std::memcmp(compact.data() + 1200, original.data() + 1208, 8));
        CHECK(!std::memcmp(compact.data() + 1208, original.data() + 1232, 8));
        auto invalid = original; invalid[0x108] = 9;
        const auto before = invalid;
        CHECK(!crabe::di1::compactParticleEnvelopes(invalid.data(), invalid.size())); CHECK(invalid == before);
        CHECK(crabe::di1::filterParticleDirectory(compact.data(), compact.size(), [](unsigned, unsigned, unsigned) { return false; }));
        CHECK(crabe::di1::compactParticleEnvelopes(compact.data(), compact.size()));
        CHECK(view.parse(compact.data(), compact.size()) && view.curves == 0);
    }
    if (argc >= 3) {
        std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
        const std::vector<unsigned char> stock((std::istreambuf_iterator<char>(input)), {});
        crabe::di1::ParticleDirectory before;
        CHECK(before.parse(stock.data(), stock.size()));
        if (before.data && before.size >= 20) {
            auto identity = stock;
            CHECK(crabe::di1::filterParticleDirectory(identity.data(), identity.size(), [](unsigned, unsigned, unsigned) { return true; }));
            CHECK(identity == stock);
            auto subset = stock;
            CHECK(crabe::di1::filterParticleDirectory(subset.data(), subset.size(), [](unsigned, unsigned id, unsigned) { return id % 3 != 0; }));
            crabe::di1::ParticleDirectory after; CHECK(after.parse(subset.data(), subset.size()));
            CHECK(!std::memcmp(stock.data(), subset.data(), before.envelopes + before.curves * 8));
            for (unsigned kind = 0; kind < 3; ++kind) {
                unsigned retained = 0;
                for (unsigned i = 0; i < before.count[kind]; ++i) {
                    const auto* row = before.row(kind, i);
                    if (before.read<unsigned>(row + 4) % 3 == 0) continue;
                    CHECK(!std::memcmp(row, after.row(kind, retained++), before.stride[kind]));
                }
                CHECK(after.count[kind] == retained);
            }
            const auto uncompact = subset;
            const auto previous = after;
            CHECK(crabe::di1::compactParticleEnvelopes(subset.data(), subset.size()));
            CHECK(after.parse(subset.data(), subset.size()));
            for (unsigned kind : {0u, 2u})
                for (unsigned i = 0; i < previous.count[kind]; ++i) {
                    const auto offset = previous.read<unsigned>(previous.row(kind, i));
                    const unsigned start = kind == 0 ? 0x108 : 0;
                    const unsigned count = kind == 0 ? uncompact[offset + 0x105] : 6;
                    for (unsigned j = 0; j < count; ++j) {
                        const auto oldId = previous.read<unsigned short>(uncompact.data() + offset + start + j * 2);
                        const auto newId = after.read<unsigned short>(subset.data() + offset + start + j * 2);
                        if (oldId == 0xFFFF) CHECK(newId == oldId);
                        else {
                            CHECK(newId < after.curves);
                            CHECK(!std::memcmp(uncompact.data() + previous.envelopes + oldId * 8,
                                subset.data() + after.envelopes + newId * 8, 8));
                        }
                    }
                }
            CHECK(crabe::di1::filterParticleDirectory(subset.data(), subset.size(), [](unsigned, unsigned, unsigned) { return false; }));
            CHECK(after.parse(subset.data(), subset.size()));
            CHECK(after.count[0] == 0 && after.count[1] == 0 && after.count[2] == 0);
        }
    }
    CharacterPlaysets ownership;
    ownership.add("Incredibles", "AV_MrIncredible,AV_ElastiGirl,AV_Dash,AV_Violet,AV_Syndrome");
    ownership.add("Cars", "AV_McQueen,AV_Holly,AV_Mater,AV_Francesco");
    ownership.add("MonstersU", "MU_Sully,MU_Mike");
    ownership.add("Pirates", "PIR_JackSparrow,PIR_Barbossa,PIR_DavyJones");
    ownership.add("ToyStoryInSpace", "AV_Buzz,AV_Jessie,AV_Woody");
    ownership.add("LoneRanger", "LR_LoneRanger,LR_Tonto");
    ownership.add("RumpusRoom", "AV_MrIncredible,PNF_Perry,PNF_Phineas,AV_Elsa,AV_Ralph");
    for (const auto* playset : {"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"}) {
        CHECK(ownership.foreign("PNF_Perry", playset));
        CHECK(ownership.foreign("PNF_PerryInfinite", playset));
        CHECK(ownership.foreign("AV_Elsa", playset));
        CHECK(ownership.foreign("AV_Ralph", playset));
    }
    CHECK(!ownership.foreign("PNF_Perry", "RumpusRoom"));
    CharacterPlaysets reversed;
    reversed.add("RumpusRoom", "AV_MrIncredibleInfinite,AV_MrIncredible,PNF_PerryInfinite,PNF_Perry");
    reversed.add("Incredibles", "AV_MrIncredible");
    CHECK(!reversed.foreign("AV_MrIncredible", "Incredibles"));
    CHECK(!reversed.foreign("AV_MrIncredibleInfinite", "Incredibles"));
    CHECK(reversed.foreign("AV_MrIncredible", "Pirates"));
    CHECK(reversed.foreign("PNF_Perry", "Incredibles"));
    CHECK(!ownership.foreign("av_mrincredible", "Incredibles"));
    CHECK(ownership.foreign("AV_MrIncredible", "Pirates"));
    CHECK(ownership.foreign("PIR_JackSparrow", "Incredibles"));
    CHECK(!ownership.foreign("PIR_JackSparrow", "Pirates"));
    CHECK(ownership.foreign("MU_SullyInfinite", "ToyStoryInSpace"));
    CHECK(!ownership.foreign("MU_SullyInfinite", "MonstersU"));
    CHECK(!ownership.foreign("AV_Cars_Francesco", "Cars"));
    CHECK(ownership.foreign("AV_Cars_Francesco", "Incredibles"));
    CHECK(ownership.foreign("av_mcqueeninfinite", "Pirates"));
    CHECK(!ownership.foreign("av_mcqueeninfinite", "Cars"));
    CHECK(!ownership.foreign("AV_Buzz", "ToyStoryInSpace"));
    CHECK(ownership.foreign("AV_Buzz", "LoneRanger"));
    CHECK(!ownership.foreign("AV_Buzz", "RumpusRoom"));
    CHECK(!ownership.foreign("AV_Buzz", "Miscellaneous"));
    CHECK(!ownership.foreign("AV_Buzz", ""));
    CHECK(!ownership.foreign("INTRO_Spark", "Pirates"));
    CHECK(!ownership.foreign("Mod_CustomAvatar", "Pirates"));
    CHECK(!ownership.foreign("AV_Buzz_UnknownVariant", "Incredibles"));
    // Reloading data must not change the home of a native avatar or break a mod's own avatar.
    ownership.add("ToyStoryInSpace", " AV_Buzz ,AV_Jessie,,AV_Woody,Mod_CustomAvatar ");
    CHECK(!ownership.foreign("Mod_CustomAvatar", "ToyStoryInSpace"));
    CHECK(ownership.foreign("Mod_CustomAvatar", "Pirates"));
    CHECK(!ownership.foreign("AV_Buzz", "ToyStoryInSpace"));
    std::printf("Character variant policy: %s\n", failures ? "FAILED" : "passed");
    // Optional integration with a supplied EXE: map a private image without running its
    // entry point. The real installer must pass every signature and restore the entire text
    // section on rollback. The file on disk is never opened for writing or executed.
    if (argc >= 2) {
        const auto image = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
        CHECK(image != nullptr);
        if (image) {
            auto* base = reinterpret_cast<unsigned char*>(image);
            const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
            const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
            const auto* sections = IMAGE_FIRST_SECTION(nt);
            const IMAGE_SECTION_HEADER* code = nullptr;
            for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
                if (sections[i].Characteristics & IMAGE_SCN_CNT_CODE) { code = &sections[i]; break; }
            CHECK(code != nullptr);
            if (code) {
                auto* bytes = base + code->VirtualAddress;
                const std::vector<unsigned char> snapshot(bytes, bytes + code->Misc.VirtualSize);
                crabe::di1::Config options;
                CHECK(MH_Initialize() == MH_OK);
                CHECK(crabe::di1::installCharacterFreedom(image, options));
                CHECK(!crabe::di1::characterFreedomActive());
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                options.charactersInAnyPlayset = true;
                CHECK(crabe::di1::installCharacterFreedom(image, options));
                CHECK(crabe::di1::characterFreedomActive());
                // Drive the installed x86 detour and real ownership adapter with stand-ins for
                // the two engine dependencies. This exercises their calling conventions and
                // delegation without starting an engine, creating actors, or loading a save.
                crabe::mods::LuaApi api;
                api.toLString = &testLuaString;
                crabe::di1::addCharacterFreedomAdapters(api);
                CHECK(api.adapters.size() == 1);
                if (api.adapters.size() == 1) api.adapters[0].function(nullptr);
                auto** managerSlot = *reinterpret_cast<void***>(base + 0xB01FD7);
                const auto previousManager = *managerSlot;
                unsigned char testManager[0x158]{};
                *managerSlot = testManager;
                // mov eax,[testCurrentPlayset]; ret 16
                unsigned char getString[] = {0xA1, 0, 0, 0, 0, 0xC2, 0x10, 0};
                const auto currentPointer = reinterpret_cast<std::uintptr_t>(&testCurrentPlayset);
                std::memcpy(getString + 1, &currentPointer, 4);
                CHECK(replaceCode(base + 0xAFE4B0, getString, sizeof(getString)));
                // The original trampoline executes its six-byte SEH read and jumps here.
                // mov eax,[esp+4]; mov [eax],nativeMarker; ret 4
                static const char nativeMarker[] = "native_variant";
                unsigned char native[] = {0x8B, 0x44, 0x24, 0x04, 0xC7, 0x00, 0, 0, 0, 0, 0xC2, 4, 0};
                const auto markerPointer = reinterpret_cast<std::uintptr_t>(nativeMarker);
                std::memcpy(native + 6, &markerPointer, 4);
                CHECK(replaceCode(base + 0xB01FC6, native, sizeof(native)));
                const auto variant = reinterpret_cast<void(__thiscall*)(void*, const char**)>(base + 0xB01FC0);
                const char* actor = "av_mrincredible";
                variant(nullptr, &actor);
                CHECK(!std::strcmp(actor, "av_mrincredible"));
                testCurrentPlayset = "Incredibles";
                variant(nullptr, &actor);
                CHECK(actor == nativeMarker);
                testCurrentPlayset = "RumpusRoom";
                actor = "av_mrincredible";
                variant(nullptr, &actor);
                CHECK(actor == nativeMarker);
                actor = "mod_unknown";
                testCurrentPlayset = "Pirates";
                variant(nullptr, &actor);
                CHECK(actor == nativeMarker);
                *managerSlot = nullptr;
                actor = "av_mrincredible";
                variant(nullptr, &actor);
                CHECK(actor == nativeMarker);
                *managerSlot = previousManager;
                CHECK(replaceCode(base + 0xAFE4B0, snapshot.data() + 0xAFE4B0 - code->VirtualAddress, sizeof(getString)));
                CHECK(replaceCode(base + 0xB01FC6, snapshot.data() + 0xB01FC6 - code->VirtualAddress, sizeof(native)));
                // Four swap branches and all character-to-playset lookup code stay original.
                for (auto rva : {0x77E1D0u, 0x77E1DAu, 0x77E1E8u, 0x77E1F6u, 0x8CB50u})
                    CHECK(!std::memcmp(base + rva, snapshot.data() + rva - code->VirtualAddress, 2));
                crabe::di1::removeCharacterFreedom();
                CHECK(!crabe::di1::characterFreedomActive());
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                // Optional EXP diagnostics must preserve every original argument and call,
                // including after the log budget is exhausted. The native award stand-in
                // receives the hook's actual x86 trampoline/fastcall call path.
                for (unsigned mode = 0; mode < 3; ++mode) {
                options.traceCharacterFreedom = mode != 2;
                options.testExperienceMultiplier = mode == 0 ? 1 : 5;
                observedAwards = 0;
                CHECK(crabe::di1::installCharacterFreedom(image, options));
                CHECK(base[0x759D60] == 0xE9);
                // The trampoline copies mov eax,[esp+4]; push esi, then jumps to +5.
                // Balance that push and jump to a recording stand-in with the same ABI.
                unsigned char awardStub[] = {0x5E, 0xE9, 0, 0, 0, 0};
                const auto awardRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureAward) -
                    reinterpret_cast<std::uintptr_t>(base + 0x759D60 + 5 + sizeof(awardStub)));
                std::memcpy(awardStub + 2, &awardRelative, 4);
                CHECK(replaceCode(base + 0x759D65, awardStub, sizeof(awardStub)));
                const auto award = reinterpret_cast<void(__thiscall*)(void*, unsigned, float, bool)>(base + 0x759D60);
                for (unsigned i = 0; i < 130; ++i) {
                    award(testManager, 0x12345678 + i, 7.25f + i, (i & 1) != 0);
                    CHECK(observedActor == 0x12345678 + i);
                    CHECK(observedAmount == (7.25f + i) * options.testExperienceMultiplier);
                    CHECK(observedTracked == ((i & 1) != 0));
                    CHECK(observedSelf == testManager);
                }
                CHECK(observedAwards == 130);
                for (float amount : {0.f, -2.f}) {
                    award(testManager, 42, amount, false);
                    CHECK(observedAmount == amount);
                }
                CHECK(replaceCode(base + 0x759D65, snapshot.data() + 0x759D65 - code->VirtualAddress, sizeof(awardStub)));
                if (options.traceCharacterFreedom) {
                    CHECK(base[0x79CA60] == 0xE9);
                    CHECK(base[0xBCD2D0] == 0xE9);
                    CHECK(base[0xC1EEE0] == 0xE9);
                    CHECK(base[0xB30000] == 0xE9);
                    CHECK(base[0xC1E1C0] == 0xE9);
                    CHECK(base[0x2E6130] == 0xE9);
                    CHECK(base[0xC1A600] == 0xE9);
                    CHECK(base[0x614E00] == 0xE9);
                    CHECK(base[0x2D8AC0] == 0xE9);
                    // Both trampolines copy push -1; push SEH-handler (seven bytes).
                    // Balance those pushes before recording the original call arguments.
                    unsigned char observerStub[] = {0x83, 0xC4, 8, 0xE9, 0, 0, 0, 0};
                    const auto replaceObserver = [&](unsigned rva, std::uintptr_t capture) {
                        const auto relative = static_cast<std::int32_t>(capture - reinterpret_cast<std::uintptr_t>(base + rva + 7 + sizeof(observerStub)));
                        std::memcpy(observerStub + 4, &relative, 4);
                        CHECK(replaceCode(base + rva + 7, observerStub, sizeof(observerStub)));
                    };
                    replaceObserver(0x79CA60, reinterpret_cast<std::uintptr_t>(&captureRule));
                    replaceObserver(0xBCD2D0, reinterpret_cast<std::uintptr_t>(&captureModel));
                    // Initialization's trampoline copies sub esp,40h; push ebx; mov ebx,ecx.
                    unsigned char initializeStub[] = {0x5B, 0x83, 0xC4, 0x40, 0xE9, 0, 0, 0, 0};
                    const auto initializeRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureInitialize) -
                        reinterpret_cast<std::uintptr_t>(base + 0xC1EEE0 + 6 + sizeof(initializeStub)));
                    std::memcpy(initializeStub + 5, &initializeRelative, 4);
                    CHECK(replaceCode(base + 0xC1EEE6, initializeStub, sizeof(initializeStub)));
                    const auto initializeCall = reinterpret_cast<void(__thiscall*)(void*)>(base + 0xC1EEE0);
                    unsigned char toolManager[0x390]{};
                    const auto ruleCall = reinterpret_cast<void(__thiscall*)(void*, void*)>(base + 0x79CA60);
                    const auto modelCall = reinterpret_cast<bool(__thiscall*)(void*, const char*, unsigned, unsigned)>(base + 0xBCD2D0);
                    unsigned char rule[0x38]{};
                    unsigned char modelComponent[16]{};
                    static const char model[] = "Avatar_McQueen_Exclusive";
                    observedRules = observedModels = observedInitializations = 0;
                    unsigned char dataStub[] = {0xE9, 0, 0, 0, 0};
                    const auto setJump = [&](unsigned rva, std::uintptr_t capture) {
                        const auto relative = static_cast<std::int32_t>(capture - reinterpret_cast<std::uintptr_t>(base + rva + sizeof(dataStub)));
                        std::memcpy(dataStub + 1, &relative, 4);
                        CHECK(replaceCode(base + rva, dataStub, sizeof(dataStub)));
                    };
                    setJump(0x283E50, reinterpret_cast<std::uintptr_t>(&captureData));
                    // Getter trampoline copies push esi/push edi/mov edi,[esp+12].
                    // Keep the installed hook entry intact, balance pushes in its body.
                    unsigned char getterStub[] = {0x5F, 0x5E, 0xE9, 0, 0, 0, 0};
                    const auto getterRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureContextGet) -
                        reinterpret_cast<std::uintptr_t>(base + 0xB30096 + sizeof(getterStub)));
                    std::memcpy(getterStub + 3, &getterRelative, 4);
                    CHECK(replaceCode(base + 0xB30096, getterStub, sizeof(getterStub)));
                    unsigned owner[2] = {0, 0x12345678};
                    *reinterpret_cast<void**>(toolManager + 4) = owner;
                    observedContextReads = 0;
                    for (unsigned i = 0; i < 130; ++i) {
                        ruleCall(rule, nullptr);
                        CHECK(observedSelf == rule && observedRuleTools == nullptr);
                        const unsigned first = i % 3 ? i & 1 : 0xABCDEF00u + i;
                        const unsigned second = i % 3 ? (i & 2) >> 1 : first;
                        CHECK(modelCall(modelComponent, model, first, second) == (first != second));
                        CHECK(observedSelf == modelComponent && observedModel == model);
                        CHECK(observedFirst == first && observedSecond == second);
                        initializeCall(toolManager);
                        CHECK(observedSelf == toolManager);
                    }
                    CHECK(observedRules == 130 && observedModels == 130);
                    CHECK(observedInitializations == 130);
                    CHECK(observedDataActor == owner[1]);
                    const auto* typeSlot = *reinterpret_cast<const unsigned short* const*>(base + 0x83E0FE);
                    CHECK(observedDataType == *typeSlot);
                    CHECK(observedContextReads == 64 * 17);
                    *reinterpret_cast<void**>(toolManager + 4) = nullptr;
                    CHECK(replaceCode(base + 0x283E50, snapshot.data() + 0x283E50 - code->VirtualAddress, sizeof(dataStub)));
                    CHECK(replaceCode(base + 0xB30096, snapshot.data() + 0xB30096 - code->VirtualAddress, sizeof(getterStub)));
                    // Setter's trampoline pushes ebx/esi/edi then copies mov edi,[esp+16].
                    // Action trampoline copies mov eax,[esp+4]; push esi.
                    unsigned char controlStub[] = {0x5F, 0x5E, 0x5B, 0xE9, 0, 0, 0, 0};
                    const auto controlRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureControl) -
                        reinterpret_cast<std::uintptr_t>(base + 0xB30007 + sizeof(controlStub)));
                    std::memcpy(controlStub + 4, &controlRelative, 4);
                    CHECK(replaceCode(base + 0xB30007, controlStub, sizeof(controlStub)));
                    unsigned char actionStub[] = {0x5E, 0xE9, 0, 0, 0, 0};
                    const auto actionRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureAction) -
                        reinterpret_cast<std::uintptr_t>(base + 0xC1E1C5 + sizeof(actionStub)));
                    std::memcpy(actionStub + 2, &actionRelative, 4);
                    CHECK(replaceCode(base + 0xC1E1C5, actionStub, sizeof(actionStub)));
                    const auto controlCall = reinterpret_cast<void(__thiscall*)(void*, const char**, unsigned)>(base + 0xB30000);
                    const auto actionCall = reinterpret_cast<void(__thiscall*)(void*, const char*, const char*, unsigned, unsigned)>(base + 0xC1E1C0);
                    const char* context = "DisableToolSelection";
                    unsigned stateActor = 0x12345678;
                    observedControls = observedActions = 0;
                    for (unsigned i = 0; i < 514; ++i) {
                        const auto value = 0xABCDEF00u + i;
                        controlCall(&stateActor, &context, value);
                        CHECK(observedSelf == &stateActor && observedContext == &context && observedControlValue == value);
                        CHECK(!std::strcmp(context, "DisableToolSelection"));
                    }
                    CHECK(observedControls == 514 && stateActor == 0x12345678);
                    for (unsigned i = 0; i < 130; ++i) {
                        const char* group = "Items";
                        const char* action = "Throw";
                        actionCall(toolManager, group, action, i, 0xABCDEF00u + i);
                        CHECK(observedSelf == toolManager && observedGroup == group && observedAction == action);
                        CHECK(observedFirst == i && observedSecond == 0xABCDEF00u + i);
                    }
                    CHECK(observedActions == 130);
                    // Asset availability forwards the borrowed name, full type word
                    // and native bool unchanged, including past its diagnostic budget.
                    unsigned char assetStub[] = {0xE9, 0, 0, 0, 0};
                    const auto assetRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureAsset) -
                        reinterpret_cast<std::uintptr_t>(base + 0x2E6138 + sizeof(assetStub)));
                    std::memcpy(assetStub + 1, &assetRelative, 4);
                    CHECK(replaceCode(base + 0x2E6138, assetStub, sizeof(assetStub)));
                    const auto assetCall = reinterpret_cast<bool(__thiscall*)(void*, const char*, unsigned)>(base + 0x2E6130);
                    static const char assetName[] = "Tool_CustomProjectile";
                    observedAssetCalls = 0;
                    for (unsigned i = 0; i < 260; ++i) {
                        CHECK(!assetCall(testManager, assetName, 2));
                        CHECK(observedSelf == testManager && observedAssetName == assetName && observedAssetType == 2);
                    }
                    CHECK(assetCall(testManager, assetName, 0xABCDEF01));
                    CHECK(observedAssetType == 0xABCDEF01 && observedAssetCalls == 261);
                    // Query trampoline copies mov eax,[esp+4]; push ebx. Execute
                    // through a private thunk in the verified auto-equip caller range
                    // to exercise logging past its budget as well as forwarding.
                    unsigned char unlockedStub[] = {0x5B, 0xE9, 0, 0, 0, 0};
                    const auto unlockedRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureUnlocked) -
                        reinterpret_cast<std::uintptr_t>(base + 0xC1A605 + sizeof(unlockedStub)));
                    std::memcpy(unlockedStub + 2, &unlockedRelative, 4);
                    CHECK(replaceCode(base + 0xC1A605, unlockedStub, sizeof(unlockedStub)));
                    unsigned char queryThunk[] = {0x8B, 0x44, 0x24, 4, 0x50, 0xE8, 0, 0, 0, 0, 0xC2, 4, 0};
                    const auto queryRelative = static_cast<std::int32_t>((base + 0xC1A600) - (base + 0x8B61D1 + 10));
                    std::memcpy(queryThunk + 6, &queryRelative, 4);
                    CHECK(replaceCode(base + 0x8B61D1, queryThunk, sizeof(queryThunk)));
                    const auto queryCall = reinterpret_cast<bool(__thiscall*)(void*, const char*)>(base + 0x8B61D1);
                    static const char queryName[] = "ITEM_TestProjectile";
                    observedUnlockedCalls = 0;
                    for (unsigned i = 0; i < 130; ++i) {
                        testUnlockedResult = bool(i & 1);
                        CHECK(queryCall(toolManager, queryName) == testUnlockedResult);
                        CHECK(observedSelf == toolManager && observedUnlockedName == queryName);
                    }
                    CHECK(observedUnlockedCalls == 130);
                    // Projectile trampoline copies only the six-byte FS read, so no
                    // pushes need balancing. Preserve all24 bytes of cdecl arguments,
                    // vector bits, owner pointer and returned actor past128 events.
                    unsigned char projectileStub[] = {0xE9, 0, 0, 0, 0};
                    const auto projectileRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureProjectile) -
                        reinterpret_cast<std::uintptr_t>(base + 0x614E06 + sizeof(projectileStub)));
                    std::memcpy(projectileStub + 1, &projectileRelative, 4);
                    CHECK(replaceCode(base + 0x614E06, projectileStub, sizeof(projectileStub)));
                    const auto projectileCall = reinterpret_cast<void*(__cdecl*)(unsigned, ProjectileVector, unsigned, void*)>(base + 0x614E00);
                    unsigned projectileOwner[2] = {0, 0x12345678};
                    observedProjectileCalls = 0;
                    for (unsigned i = 0; i < 130; ++i) {
                        const unsigned flags = 0xABCDEF00u + i, kind = 0x50524854u + i;
                        const ProjectileVector position = {i + .25f, -static_cast<float>(i) - .5f, .00001f};
                        void* const ownerPointer = i % 3 ? projectileOwner : nullptr;
                        CHECK(projectileCall(flags, position, kind, ownerPointer) == (flags & 1 ? nullptr : projectileResultActor));
                        CHECK(observedProjectileFlags == flags && observedProjectileKind == kind);
                        CHECK(!std::memcmp(&observedProjectilePosition, &position, sizeof(position)));
                        CHECK(observedProjectileOwner == ownerPointer);
                    }
                    CHECK(observedProjectileCalls == 130);
                    // Emitter registry trampoline copies mov eax,[esp+4]; push eax.
                    // A private thunk reproduces the native emitter-init return site;
                    // verify full IDs, pointers and forwarding beyond256 trace pairs.
                    unsigned char emitterStub[] = {0x83, 0xC4, 4, 0xE9, 0, 0, 0, 0};
                    const auto emitterRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureEmitter) -
                        reinterpret_cast<std::uintptr_t>(base + 0x2D8AC5 + sizeof(emitterStub)));
                    std::memcpy(emitterStub + 4, &emitterRelative, 4);
                    CHECK(replaceCode(base + 0x2D8AC5, emitterStub, sizeof(emitterStub)));
                    const auto emitterQueryRelative = static_cast<std::int32_t>((base + 0x2D8AC0) - (base + 0x2EF8D8 + 10));
                    std::memcpy(queryThunk + 6, &emitterQueryRelative, 4);
                    CHECK(replaceCode(base + 0x2EF8D8, queryThunk, sizeof(queryThunk)));
                    const auto emitterCall = reinterpret_cast<void*(__thiscall*)(void*, unsigned)>(base + 0x2EF8D8);
                    observedEmitterCalls = 0;
                    for (unsigned i = 0; i < 260; ++i) {
                        const unsigned id = 0xDE5BA700u + i;
                        CHECK(emitterCall(testManager, id) == (id & 1 ? nullptr : projectileResultActor));
                        CHECK(observedEmitterId == id && observedSelf == testManager);
                    }
                    CHECK(observedEmitterCalls == 260);
                    CHECK(replaceCode(base + 0x2D8AC5, snapshot.data() + 0x2D8AC5 - code->VirtualAddress, sizeof(emitterStub)));
                    CHECK(replaceCode(base + 0x2EF8D8, snapshot.data() + 0x2EF8D8 - code->VirtualAddress, sizeof(queryThunk)));
                    CHECK(replaceCode(base + 0x614E06, snapshot.data() + 0x614E06 - code->VirtualAddress, sizeof(projectileStub)));
                    CHECK(replaceCode(base + 0xC1A605, snapshot.data() + 0xC1A605 - code->VirtualAddress, sizeof(unlockedStub)));
                    CHECK(replaceCode(base + 0x8B61D1, snapshot.data() + 0x8B61D1 - code->VirtualAddress, sizeof(queryThunk)));
                    CHECK(replaceCode(base + 0x2E6138, snapshot.data() + 0x2E6138 - code->VirtualAddress, sizeof(assetStub)));
                    CHECK(replaceCode(base + 0xB30007, snapshot.data() + 0xB30007 - code->VirtualAddress, sizeof(controlStub)));
                    CHECK(replaceCode(base + 0xC1E1C5, snapshot.data() + 0xC1E1C5 - code->VirtualAddress, sizeof(actionStub)));
                    CHECK(replaceCode(base + 0xC1EEE6, snapshot.data() + 0xC1EEE6 - code->VirtualAddress, sizeof(initializeStub)));
                    for (auto rva : {0x79CA60u, 0xBCD2D0u})
                        CHECK(replaceCode(base + rva + 7, snapshot.data() + rva + 7 - code->VirtualAddress, sizeof(observerStub)));
                } else {
                    CHECK(!std::memcmp(base + 0x79CA60, snapshot.data() + 0x79CA60 - code->VirtualAddress, 7));
                    CHECK(!std::memcmp(base + 0xBCD2D0, snapshot.data() + 0xBCD2D0 - code->VirtualAddress, 7));
                    CHECK(!std::memcmp(base + 0xC1EEE0, snapshot.data() + 0xC1EEE0 - code->VirtualAddress, 6));
                    CHECK(base[0xB30000] == 0xE9); // Menu writes also work with trace off.
                    CHECK(!std::memcmp(base + 0xC1E1C0, snapshot.data() + 0xC1E1C0 - code->VirtualAddress, 5));
                    CHECK(!std::memcmp(base + 0x2E6130, snapshot.data() + 0x2E6130 - code->VirtualAddress, 8));
                    CHECK(base[0xC1A600] == 0xE9); // Availability repair also works with trace off.
                    CHECK(!std::memcmp(base + 0x614E00, snapshot.data() + 0x614E00 - code->VirtualAddress, 6));
                    CHECK(base[0x2D8AC0] == 0xE9); // Observer delegates; preload runs independently of tracing.
                }
                crabe::di1::removeCharacterFreedom();
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                }
                options.traceCharacterFreedom = false;
                options.testExperienceMultiplier = 1;
                CHECK(crabe::di1::installCharacterFreedom(image, options));
                CHECK(base[0xB30090] == 0xE9 && base[0xC1D200] == 0xE9);
                // Exercise both functional hooks with native engine stand-ins. They
                // must forward once and exempt only actual players in six playsets.
                unsigned char getStub[] = {0x5F, 0x5E, 0xE9, 0, 0, 0, 0};
                const auto getRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureMenuGet) -
                    reinterpret_cast<std::uintptr_t>(base + 0xB30096 + sizeof(getStub)));
                std::memcpy(getStub + 3, &getRelative, 4);
                CHECK(replaceCode(base + 0xB30096, getStub, sizeof(getStub)));
                unsigned char jumpStub[] = {0xE9, 0, 0, 0, 0};
                const auto jump = [&](unsigned rva, std::uintptr_t capture) {
                    const auto relative = static_cast<std::int32_t>(capture - reinterpret_cast<std::uintptr_t>(base + rva + sizeof(jumpStub)));
                    std::memcpy(jumpStub + 1, &relative, 4);
                    CHECK(replaceCode(base + rva, jumpStub, sizeof(jumpStub)));
                };
                jump(0xC1D206, reinterpret_cast<std::uintptr_t>(&captureWorld));
                jump(0xAE8070, reinterpret_cast<std::uintptr_t>(&capturePlayer));
                unsigned char setStub[] = {0x5F, 0x5E, 0x5B, 0xE9, 0, 0, 0, 0};
                const auto setRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureControl) -
                    reinterpret_cast<std::uintptr_t>(base + 0xB30007 + sizeof(setStub)));
                std::memcpy(setStub + 4, &setRelative, 4);
                CHECK(replaceCode(base + 0xB30007, setStub, sizeof(setStub)));
                CHECK(replaceCode(base + 0xAFE4B0, getString, sizeof(getString)));
                auto** playerSlot = *reinterpret_cast<void***>(base + 0x759D6A);
                const auto previousPlayers = *playerSlot;
                *playerSlot = testManager; *managerSlot = testManager;
                unsigned toolOwner[] = {0, 0x12345678};
                unsigned char testTools[0x390]{};
                *reinterpret_cast<void**>(testTools + 4) = toolOwner;
                unsigned char toolParent[0x7C]{};
                *reinterpret_cast<void**>(toolParent) = testTools;
                toolParent[0x78] = 4;
                unsigned stateActor = toolOwner[1];
                const auto getMenu = reinterpret_cast<bool(__thiscall*)(void*, const char**)>(base + 0xB30090);
                const auto getWorld = reinterpret_cast<int(__thiscall*)(void*, const void*)>(base + 0xC1D200);
                const auto setMenu = reinterpret_cast<void(__thiscall*)(void*, const char**, unsigned)>(base + 0xB30000);
                const auto getUnlocked = reinterpret_cast<bool(__thiscall*)(void*, const char*)>(base + 0xC1A600);
                const char* context = "TUT_DISABLE_SELECT_ITEM";
                int zones = 42;
                observedGetCalls = observedWorldCalls = 0;
                testContextResult = true;
                for (const char* playset : {"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"}) {
                    testCurrentPlayset = playset;
                    for (unsigned i = 0; i < 130; ++i) {
                        CHECK(!getMenu(&stateActor, &context));
                        CHECK(observedContext == &context && observedSelf == &stateActor && observedActor == stateActor);
                        CHECK(getWorld(toolParent, &zones) == 1);
                        CHECK(observedSelf == toolParent && observedZones == &zones);
                    }
                }
                CHECK(observedGetCalls == 780 && observedWorldCalls == 780);
                testCurrentPlayset = "Incredibles";
                observedControls = 0;
                for (unsigned i = 0; i < 514; ++i) {
                    const auto requested = 0xABCD0001u + i * 0x100;
                    setMenu(&stateActor, &context, requested);
                    CHECK(observedControlValue == (requested & ~0xFFu));
                    CHECK(observedContext == &context && observedSelf == &stateActor);
                }
                CHECK(observedControls == 514); // The dictionary receives false, not just getter override.
                setMenu(&stateActor, &context, 0); CHECK(observedControlValue == 0);
                for (const char* playset : {"RumpusRoom", "Miscellaneous", "Mod_CustomWorld", ""}) {
                    testCurrentPlayset = playset;
                    CHECK(getMenu(&stateActor, &context)); CHECK(getWorld(toolParent, &zones) == 0);
                    setMenu(&stateActor, &context, 0xABCD0001); CHECK(observedControlValue == 0xABCD0001);
                }
                testCurrentPlayset = "Incredibles";
                for (const char* key : {"DisableToolSelection", "TardisDisableToolSelection", "racemission", "IGP_Active"}) {
                    context = key; CHECK(getMenu(&stateActor, &context));
                    setMenu(&stateActor, &context, 0xABCD0001); CHECK(observedControlValue == 0xABCD0001);
                }
                context = "TUT_DISABLE_SELECT_ITEM";
                testBlockedUse = true; CHECK(getMenu(&stateActor, &context));
                setMenu(&stateActor, &context, 1); CHECK(observedControlValue == 1);
                testBlockedUse = false; testBlockedSpectator = true; CHECK(getMenu(&stateActor, &context));
                setMenu(&stateActor, &context, 1); CHECK(observedControlValue == 1);
                testBlockedSpectator = false;
                for (unsigned flags : {0u, 1u, 5u}) {
                    toolParent[0x78] = static_cast<unsigned char>(flags); CHECK(getWorld(toolParent, &zones) == 0);
                }
                toolParent[0x78] = 4;
                for (int player : {-1, 4}) {
                    testPlayerIndex = player;
                    CHECK(getMenu(&stateActor, &context)); CHECK(getWorld(toolParent, &zones) == 0);
                    setMenu(&stateActor, &context, 0xABCD0001); CHECK(observedControlValue == 0xABCD0001);
                }
                testPlayerIndex = 0;
                testContextResult = false; CHECK(!getMenu(&stateActor, &context));
                testWorldResult = 2; CHECK(getWorld(toolParent, &zones) == 2);
                testWorldResult = 0; CHECK(getWorld(toolParent, nullptr) == 0);
                // Inventory can cache a failed zone check before player registration.
                // Refresh only that state, through the native bool(false) ABI. Test
                // every playset and player slot with trace disabled, including repeated
                // queries after success and progression-denied retries.
                unsigned char unlockedStub[] = {0x5B, 0xE9, 0, 0, 0, 0};
                const auto unlockedRelative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureUnlocked) -
                    reinterpret_cast<std::uintptr_t>(base + 0xC1A605 + sizeof(unlockedStub)));
                std::memcpy(unlockedStub + 2, &unlockedRelative, 4);
                CHECK(replaceCode(base + 0xC1A605, unlockedStub, sizeof(unlockedStub)));
                jump(0xC1E280, reinterpret_cast<std::uintptr_t>(&captureRefresh));
                void* entries[] = {toolParent};
                *reinterpret_cast<void***>(testTools + 0x2BC) = entries;
                *reinterpret_cast<unsigned*>(testTools + 0x2C0) = 1;
                const char* toolName = "ITEM_TestProjectile";
                *reinterpret_cast<const char**>(toolParent + 0x14) = toolName;
                *reinterpret_cast<void**>(toolParent + 0x24) = &zones;
                testRefreshEntry = toolParent; testRefreshWorld = getWorld;
                observedRefreshCalls = 0;
                for (const char* playset : {"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"}) {
                    testCurrentPlayset = playset;
                    for (int player = 0; player < 4; ++player) {
                        testPlayerIndex = player;
                        toolParent[0x78] = 2; // Preserve the engine's independent award bit.
                        CHECK(getUnlocked(testTools, "item_testprojectile"));
                        CHECK(toolParent[0x78] == 6 && !observedRefreshForced);
                        CHECK(getUnlocked(testTools, toolName));
                    }
                }
                CHECK(observedRefreshCalls == 24);
                testCurrentPlayset = "Incredibles"; testPlayerIndex = 0;
                toolParent[0x78] = 0; testProgressionUnlocked = false;
                CHECK(!getUnlocked(testTools, toolName));
                CHECK(toolParent[0x78] == 0 && observedRefreshCalls == 25);
                testProgressionUnlocked = true;
                const auto expectNoRefresh = [&] {
                    const auto before = observedRefreshCalls;
                    CHECK(!getUnlocked(testTools, toolName)); CHECK(observedRefreshCalls == before);
                };
                for (const char* playset : {"RumpusRoom", "Miscellaneous", "Mod_CustomWorld", ""}) {
                    testCurrentPlayset = playset; expectNoRefresh();
                }
                testCurrentPlayset = "Incredibles";
                for (int player : {-1, 4}) { testPlayerIndex = player; expectNoRefresh(); }
                testPlayerIndex = 0; toolParent[0x78] = 1; expectNoRefresh();
                toolParent[0x78] = 0;
                *reinterpret_cast<void**>(toolParent + 0x24) = nullptr; expectNoRefresh();
                *reinterpret_cast<void**>(toolParent + 0x24) = &zones;
                *reinterpret_cast<int*>(toolParent + 0x70) = -1; expectNoRefresh();
                *reinterpret_cast<int*>(toolParent + 0x70) = 1; expectNoRefresh();
                *reinterpret_cast<int*>(toolParent + 0x70) = 0;
                *reinterpret_cast<unsigned*>(testTools + 0x2C0) = 513; expectNoRefresh();
                *reinterpret_cast<unsigned*>(testTools + 0x2C0) = 1;
                *reinterpret_cast<void***>(testTools + 0x2BC) = nullptr; expectNoRefresh();
                *reinterpret_cast<void***>(testTools + 0x2BC) = entries;
                // Execute the native particle hooks/trampolines with a miniature
                // registry and material-bearing data file. Main-level call-site
                // thunk reproduces the verified return address B1B575.
                unsigned char loadStub[] = {0x83, 0xC4, 8, 0xE9, 0, 0, 0, 0};
                auto relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureParticleLoad) -
                    reinterpret_cast<std::uintptr_t>(base + 0x2EF6F7 + sizeof(loadStub)));
                std::memcpy(loadStub + 4, &relative, 4);
                CHECK(replaceCode(base + 0x2EF6F7, loadStub, sizeof(loadStub)));
                unsigned char importStub[] = {0x83, 0xC4, 0x0C, 0xE9, 0, 0, 0, 0};
                relative = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(&captureParticleImport) -
                    reinterpret_cast<std::uintptr_t>(base + 0x2EF357 + sizeof(importStub)));
                std::memcpy(importStub + 4, &relative, 4);
                CHECK(replaceCode(base + 0x2EF357, importStub, sizeof(importStub)));
                jump(0x2CD916, reinterpret_cast<std::uintptr_t>(&captureParticleUnload));
                jump(0xB75DF0, reinterpret_cast<std::uintptr_t>(&captureParticleFind));
                unsigned char loadThunk[] = {0x8B, 0x44, 0x24, 8, 0x50, 0x8B, 0x44, 0x24, 8, 0x50,
                    0xE8, 0, 0, 0, 0, 0x83, 0xC4, 8, 0xC3};
                relative = static_cast<std::int32_t>((base + 0x2EF6F0) - (base + 0xB1B566 + 15));
                std::memcpy(loadThunk + 11, &relative, 4);
                CHECK(replaceCode(base + 0xB1B566, loadThunk, sizeof(loadThunk)));
                const auto loadParticles = reinterpret_cast<void*(__cdecl*)(const char*, unsigned)>(base + 0xB1B566);
                const auto unloadParticles = reinterpret_cast<void(__cdecl*)(void*)>(base + 0x2CD910);
                nativeParticleImport = reinterpret_cast<bool(__cdecl*)(void*, unsigned, void*)>(base + 0x2EF350);
                auto** fxSlot = *reinterpret_cast<void***>(base + 0x2EF3BD);
                void* previousFx = *fxSlot; *fxSlot = testFxSystem;
                auto* curves = *reinterpret_cast<unsigned short**>(base + 0x2C7169);
                const auto previousCurves = *curves; *curves = 0;
                observedParticleLoads = observedParticleImports = observedParticleUnloads = 0;
                for (auto& nodes : particleNodes) nodes.clear();
                particleSource = particleImportFixture();
                *reinterpret_cast<unsigned*>(particleWorld + 0x10) = 1;
                const auto lookupEmitter = reinterpret_cast<void*(__thiscall*)(void*, unsigned)>(base + 0x2D8AC0);
                CHECK(lookupEmitter(testFxSystem + 0x18, 31) == nullptr);
                CHECK(observedParticleLoads == 0);
                // The filename/caller identifies the playset even while the zone
                // properties still report the previous Toy Box during transition.
                testCurrentPlayset = "RumpusRoom";
                CHECK(loadParticles("particles/custom_world", 0) == particleWorld);
                CHECK(observedParticleLoads == 1 && observedParticleImports == 0);
                for (unsigned kind = 0; kind < 3; ++kind)
                    particleNodes[kind].push_back({{}, testFxSystem, 10 + kind * 10});
                observedParticleLoads = 0;
                CHECK(loadParticles("particles/in_city", 0) == particleWorld);
                CHECK(observedParticleLoads == 2 && observedParticleImports == 1 && observedParticleMaterials == particleWorld);
                testCurrentPlayset = "Incredibles";
                crabe::di1::ParticleDirectory owned;
                CHECK(owned.parse(particleSource.data(), particleSource.size()));
                for (unsigned kind = 0; kind < 3; ++kind) {
                    CHECK(owned.count[kind] == 2);
                    CHECK(particleNodes[kind][0].recipe == testFxSystem); // Existing records stay native.
                }
                CHECK(owned.curves == 2);
                const auto originalImportParticles = particleImportFixture();
                CHECK(!std::memcmp(particleSource.data(), originalImportParticles.data(), 80));
                CHECK(lookupEmitter(testFxSystem + 0x18, 31) != nullptr);
                CHECK(lookupEmitter(testFxSystem + 0x18, 0xDEADBEEF) == nullptr);
                void* otherThreadResult{};
                std::thread other([&] { otherThreadResult = lookupEmitter(testFxSystem + 0x18, 31); });
                other.join(); CHECK(otherThreadResult != nullptr);
                CHECK(observedParticleLoads == 2); // One import, no retry storm for unknown emitters.
                CHECK(loadParticles("particles/in_city", 0) == particleWorld);
                CHECK(observedParticleLoads == 3 && observedParticleImports == 1);
                auto newerWorld = particleImportFixture();
                const auto unchangedNewerWorld = newerWorld;
                CHECK(nativeParticleImport(newerWorld.data(), static_cast<unsigned>(newerWorld.size()), testFxSystem));
                CHECK(observedParticleMaterials == testFxSystem);
                CHECK(owned.parse(particleSource.data(), particleSource.size()));
                CHECK(owned.count[0] == 2 && owned.count[1] == 2 && owned.count[2] == 2);
                CHECK(newerWorld == unchangedNewerWorld); // Ordinary imports remain byte-identical.
                for (unsigned kind = 0; kind < 3; ++kind)
                    for (const auto& node : particleNodes[kind])
                        CHECK(static_cast<const unsigned char*>(node.recipe) >= newerWorld.data() &&
                            static_cast<const unsigned char*>(node.recipe) < newerWorld.data() + newerWorld.size());
                unloadParticles(particleWorld);
                CHECK(observedParticleUnloads == 2 && observedParticleReleased[0] == particleSupplement && observedParticleReleased[1] == particleWorld);
                // Failed load and exhausted envelope pool do not retry or release
                // a nonexistent supplement. Cached native Toy Box files are borrowed.
                for (unsigned scenario = 0; scenario < 3; ++scenario) {
                    for (auto& nodes : particleNodes) nodes.clear();
                    particleSource = particleImportFixture(); observedParticleUnloads = 0;
                    testParticleLoadFail = scenario == 0; testParticleCached = scenario == 2;
                    *curves = scenario == 1 ? 19999 : 0;
                    const auto loads = observedParticleLoads, imports = observedParticleImports;
                    CHECK(loadParticles("particles/in_city", 0) == particleWorld);
                    CHECK(observedParticleLoads == loads + 2 && observedParticleImports == imports);
                    CHECK(lookupEmitter(testFxSystem + 0x18, 0xDEADBEEF) == nullptr);
                    CHECK(observedParticleLoads == loads + 2);
                    unloadParticles(particleWorld);
                    CHECK(observedParticleUnloads == 1 && observedParticleReleased[0] == particleWorld);
                }
                testParticleLoadFail = testParticleCached = false;
                *curves = previousCurves; *fxSlot = previousFx;
                CHECK(replaceCode(base + 0x2EF6F7, snapshot.data() + 0x2EF6F7 - code->VirtualAddress, sizeof(loadStub)));
                CHECK(replaceCode(base + 0x2EF357, snapshot.data() + 0x2EF357 - code->VirtualAddress, sizeof(importStub)));
                CHECK(replaceCode(base + 0xB1B566, snapshot.data() + 0xB1B566 - code->VirtualAddress, sizeof(loadThunk)));
                for (auto rva : {0x2CD916u, 0xB75DF0u})
                    CHECK(replaceCode(base + rva, snapshot.data() + rva - code->VirtualAddress, sizeof(jumpStub)));
                *playerSlot = nullptr;
                expectNoRefresh();
                testContextResult = true; CHECK(getMenu(&stateActor, &context)); CHECK(getWorld(toolParent, &zones) == 0);
                testRefreshEntry = nullptr;
                *playerSlot = previousPlayers; *managerSlot = previousManager;
                CHECK(replaceCode(base + 0xB30096, snapshot.data() + 0xB30096 - code->VirtualAddress, sizeof(getStub)));
                CHECK(replaceCode(base + 0xB30007, snapshot.data() + 0xB30007 - code->VirtualAddress, sizeof(setStub)));
                CHECK(replaceCode(base + 0xC1A605, snapshot.data() + 0xC1A605 - code->VirtualAddress, sizeof(unlockedStub)));
                for (auto rva : {0xC1D206u, 0xAE8070u, 0xC1E280u})
                    CHECK(replaceCode(base + rva, snapshot.data() + rva - code->VirtualAddress, sizeof(jumpStub)));
                CHECK(replaceCode(base + 0xAFE4B0, snapshot.data() + 0xAFE4B0 - code->VirtualAddress, sizeof(getString)));
                crabe::di1::removeCharacterFreedom();
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                // Another hook occupying the late world filter must roll back the
                // already installed menu hook/admission edits without removing it.
                void* occupiedWorldTrampoline{};
                CHECK(MH_CreateHook(base + 0xC1D200, reinterpret_cast<void*>(&captureWorld), &occupiedWorldTrampoline) == MH_OK);
                CHECK(!crabe::di1::installCharacterFreedom(image, options));
                CHECK(!crabe::di1::characterFreedomActive());
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                CHECK(MH_RemoveHook(base + 0xC1D200) == MH_OK);
                void* occupiedSetterTrampoline{};
                CHECK(MH_CreateHook(base + 0xB30000, reinterpret_cast<void*>(&captureControl), &occupiedSetterTrampoline) == MH_OK);
                CHECK(!crabe::di1::installCharacterFreedom(image, options));
                CHECK(!crabe::di1::characterFreedomActive());
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                CHECK(MH_RemoveHook(base + 0xB30000) == MH_OK);
                void* occupiedImportTrampoline{};
                CHECK(MH_CreateHook(base + 0x2EF350, reinterpret_cast<void*>(&captureParticleImport), &occupiedImportTrampoline) == MH_OK);
                CHECK(!crabe::di1::installCharacterFreedom(image, options));
                CHECK(!crabe::di1::characterFreedomActive());
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                CHECK(MH_RemoveHook(base + 0x2EF350) == MH_OK);
                // A mismatch in the final site must refuse the complete feature.
                auto* lastSite = base + 0x28B43A;
                DWORD protection{};
                CHECK(VirtualProtect(lastSite, 1, PAGE_EXECUTE_READWRITE, &protection));
                const auto original = *lastSite;
                *lastSite ^= 1;
                CHECK(!crabe::di1::installCharacterFreedom(image, options));
                CHECK(!crabe::di1::characterFreedomActive());
                *lastSite = original;
                CHECK(VirtualProtect(lastSite, 1, protection, &protection));
                CHECK(!std::memcmp(bytes, snapshot.data(), snapshot.size()));
                CHECK(MH_Uninitialize() == MH_OK);
            }
            FreeLibrary(image);
        }
        std::printf("Native profile installation/rollback: %s\n", failures ? "FAILED" : "passed");
    }
    return failures ? 1 : 0;
}
