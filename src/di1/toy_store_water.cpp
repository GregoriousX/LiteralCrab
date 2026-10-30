// SPDX-License-Identifier: GPL-3.0-only
#include "di1/toy_store_water.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <algorithm>
#include <array>
#include <iterator>
#include <cmath>

namespace crabe::di1 {
namespace {
// WalkInTriggerFiltered (vtable PC 0x1492CAC): Initialize (slot 10) and AcceptObject (slot 42; Wii U
// AcceptObject__21WalkInTriggerFilteredFUi), which gets every actor the trigger's collision query finds;
// ActorTypePassed decides whether it counts. Pirates' water deaths are such triggers wired to
// Respawn_Event_WATER; they lie below the sea's water surface.
constexpr unsigned triggerVtableRva = 0x1092CAC, initializeRva = 0xD955B0, acceptRva = 0xD97B30;
// ToyBoxVehicle::BeginFrame (vtable slot 16, Wii U BeginFrame__13ToyBoxVehicleFv), the game thread's
// per-frame call of every Toy Box vehicle; the owner CActor is at +4, the driver's handle at +0xE4.
constexpr unsigned frameRva = 0x7EF3A0, frameSlotRva = 0xF3DD94;
// Actor from handle, component by name, agent data by type, the name (ActorGetName) and collision
// (ActorGetCollisionExtends: GetCollisionObject, its AABB at vtable +0x1C) data types, OceanInstance
// and WaterHeightForCamera_Approx (GetOceanHeightAtPos), CActor::Kill as Meridian RespawnOrDie does.
constexpr unsigned actorRva = 0x326030, componentRva = 0x31F6B0, dataRva = 0x283E50, nameTypeRva = 0x135AEB0,
    collisionTypeRva = 0x135AD44, collisionObjectRva = 0xD3D640, oceanRva = 0x2C9540, heightRva = 0x2D4FE0,
    killRva = 0x32B770;
constexpr int respawnOrDieReason = 8;
constexpr ULONGLONG killCooldownMs = 3000;
// A vehicle riding the water surface keeps its origin 0.7-1.1 m above the waves; on a dock or a quay
// over a trigger it is higher.
constexpr float waterRideHeight = 1.3f;
// Pirates' water triggers: Water_Respawn (Buccaneer Bay), Water_Respot(_Death) (Calypso Bayou, Fort
// St. Grande, Dead Man's Cove), Death_Trigger_Main (the islets).
constexpr const char* waterTriggers[] = {"water_res", "death_trigger"};
using Initialize = int(__thiscall*)(void*);
using Accept = void(__thiscall*)(void*, std::uint32_t);
using Frame = void(__thiscall*)(void*);
using ActorFromHandle = unsigned char*(__cdecl*)(std::uint32_t);
using Component = unsigned char*(__thiscall*)(void*, const char*);
using Data = unsigned char*(__cdecl*)(std::uint32_t, std::uint32_t);
using CollisionObject = unsigned char*(__thiscall*)(void*);
using Bounds = void(__thiscall*)(void*, float*);
using Ocean = void**(__cdecl*)();
using Height = float(__thiscall*)(void*, float, float, const float*);
using Kill = void(__thiscall*)(void*, float, int);
using ActorData = void*(__thiscall*)(void*, std::uint32_t*, const std::uint32_t*);
std::uintptr_t installedBase{};
Initialize originalInitialize{}; Accept originalAccept{}; Frame originalFrame{};
ActorFromHandle actorFromHandle{}; Component component{}; Data data{}; CollisionObject collisionObject{};
Ocean ocean{}; Height height{}; Kill kill{}; ActorData actorData{};
const std::uint16_t *nameType{}, *collisionType{}, *positionType{};
void** actorManager{};
std::array<std::uint32_t, 16> triggers{}; // Water triggers of the loaded levels (handles).
struct Sunk { std::uint32_t handle{}; ULONGLONG at{}; };
std::array<Sunk, 16> sunk{};
unsigned reports{}, boundReports{}, missReports{};

const char* actorName(std::uint32_t handle) noexcept {
    const auto* named = handle ? data(handle, *nameType) : nullptr;
    return named ? *reinterpret_cast<const char* const*>(named + 8) : nullptr;
}
bool waterTrigger(const char* name) noexcept {
    if (!name) return false;
    for (const auto* prefix : waterTriggers) {
        std::size_t i = 0;
        while (prefix[i] && name[i] && (name[i] >= 'A' && name[i] <= 'Z' ? name[i] + 32 : name[i]) == prefix[i]) ++i;
        if (!prefix[i]) return true;
    }
    return false;
}
std::uint32_t ownerHandle(void* trigger) noexcept {
    const auto* owner = *reinterpret_cast<const unsigned char* const*>(static_cast<unsigned char*>(trigger) + 4);
    return owner ? *reinterpret_cast<const std::uint32_t*>(owner + 4) : 0;
}
// The synchronized world position, as the custom audio observer reads it.
bool position(std::uint32_t handle, float (&out)[3]) noexcept {
    std::uint32_t scratch{};
    const auto* row = static_cast<const unsigned char*>(actorData(*actorManager, &scratch, &handle));
    const auto* state = row ? *reinterpret_cast<const unsigned char* const*>(row + 8) : nullptr;
    if (!state) return false;
    const bool extended = (*reinterpret_cast<const std::uint16_t*>(state + 6) & 0x3FF) == *positionType;
    if (extended && (state[0x50] & 0x80)) return false; // Dirty: the engine updates it later.
    const auto* value = reinterpret_cast<const float*>(state + (extended ? 0x20C : 0x2C));
    for (unsigned i = 0; i < 3; ++i) {
        if (!std::isfinite(value[i]) || std::abs(value[i]) > 1000000) return false;
        out[i] = value[i];
    }
    return true;
}
// The trigger's collision AABB around the trigger actor (minimum xyz, maximum xyz), and the second
// vector ActorGetCollisionExtends returns (vtable +0x10), logged for reference.
bool localBounds(std::uint32_t handle, float (&box)[6], float* other = nullptr) noexcept {
    auto* collision = data(handle, *collisionType);
    auto* object = collision ? collisionObject(collision) : nullptr;
    if (!object) return false;
    reinterpret_cast<Bounds>((*reinterpret_cast<void***>(object))[0x1C / 4])(object, box);
    if (other) reinterpret_cast<Bounds>((*reinterpret_cast<void***>(object))[0x10 / 4])(object, other);
    for (const auto value : box) if (!std::isfinite(value)) return false;
    return box[3] >= box[0] && box[4] >= box[1] && box[5] >= box[2];
}
// The same box in the world: the local box moved to the trigger actor's position (without one, to
// the collision vector).
bool bounds(std::uint32_t handle, float (&box)[6]) noexcept {
    float at[3]{};
    alignas(16) float other[8]{}; // The binding leaves room for at most seven floats.
    if (!localBounds(handle, box, other)) return false;
    if (!position(handle, at)) {
        if (std::abs(other[0]) + std::abs(other[1]) + std::abs(other[2]) < 0.5f) return false;
        for (unsigned i = 0; i < 3; ++i) at[i] = other[i];
    }
    for (unsigned i = 0; i < 6; ++i) box[i] += at[i % 3];
    return true;
}
void remember(std::uint32_t handle) noexcept {
    for (const auto known : triggers) if (known == handle) return;
    auto* slot = &triggers[0];
    for (auto& known : triggers) if (!known || !actorFromHandle(known)) { slot = &known; break; }
    *slot = handle;
    float box[6]{}, at[3]{};
    alignas(16) float other[8]{}; // The binding leaves room for at most seven floats.
    if (boundReports < 16 && localBounds(handle, box, other)) {
        ++boundReports;
        const bool placed = position(handle, at);
        log("ToyStore: water trigger %s spans (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f) around %s(%.1f, %.1f, %.1f); "
            "collision vector (%.1f, %.1f, %.1f).", actorName(handle), box[0], box[1], box[2], box[3], box[4], box[5],
            placed ? "" : "unplaced ", at[0], at[1], at[2], other[0], other[1], other[2]);
    }
}
bool drivenVehicle(unsigned char* actor, const unsigned char* toyBox) noexcept {
    return actor && toyBox && *reinterpret_cast<const std::uint32_t*>(toyBox + 0xE4) && !component(actor, "ShipController");
}
// Killed like a vehicle in other worlds' water; the vehicle's native TardisRespawn sets it on land.
void sink(unsigned char* vehicle, std::uint32_t handle, const char* trigger, float y, float water) noexcept {
    const auto now = GetTickCount64();
    auto* slot = &sunk[0];
    for (auto& entry : sunk) {
        if (entry.handle == handle && now - entry.at < killCooldownMs) return;
        if (entry.at < slot->at) slot = &entry;
    }
    *slot = {handle, now};
    kill(vehicle, 0.0f, respawnOrDieReason);
    if (reports < 16) {
        ++reports;
        log("ToyStore: vehicle %08X sank over %s (height %.2f, water %.2f); native respawn.", handle, trigger, y, water);
    }
}
int __fastcall triggerInitialize(void* self, void*) {
    const auto result = originalInitialize(self);
    __try {
        const auto handle = ownerHandle(self);
        if (waterTrigger(actorName(handle))) remember(handle);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
    return result;
}
// A vehicle that went under the surface (a crash, a flip) meets the trigger itself.
void __fastcall acceptObject(void* self, void*, std::uint32_t handle) {
    originalAccept(self, handle);
    __try {
        auto* vehicle = handle ? actorFromHandle(handle) : nullptr;
        if (!vehicle || !drivenVehicle(vehicle, component(vehicle, "ToyBoxVehicle"))) return;
        const auto owner = ownerHandle(self);
        const auto* name = actorName(owner);
        if (!waterTrigger(name)) return;
        remember(owner);
        sink(vehicle, handle, name, 0.0f, 0.0f);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// A vehicle riding the water surface above a water trigger.
void __fastcall vehicleFrame(void* self, void*) {
    originalFrame(self);
    __try {
        void** surface = ocean();
        if (!surface || !*surface || !*actorManager) return; // No ocean: not Pirates.
        auto* toyBox = static_cast<unsigned char*>(self);
        auto* vehicle = *reinterpret_cast<unsigned char**>(toyBox + 4);
        if (!drivenVehicle(vehicle, toyBox)) return;
        const auto handle = *reinterpret_cast<const std::uint32_t*>(vehicle + 4);
        float at[3]{};
        if (!handle || !position(handle, at)) return;
        alignas(16) const float point[4] = {at[0], at[1], at[2], 0};
        const float water = height(*surface, at[0], at[2], point);
        if (!std::isfinite(water) || at[1] > water + waterRideHeight) return;
        float nearest[6]{};
        for (const auto trigger : triggers) {
            float box[6]{};
            if (!trigger || !actorFromHandle(trigger) || !bounds(trigger, box)) continue;
            std::copy(std::begin(box), std::end(box), nearest);
            if (at[0] < box[0] || at[0] > box[3] || at[2] < box[2] || at[2] > box[5] || at[1] < box[1]) continue;
            sink(vehicle, handle, actorName(trigger), at[1], water);
            return;
        }
        // Low on the water outside every trigger: where, for tuning (a few reports, two seconds apart).
        static ULONGLONG lastMiss{};
        const auto now = GetTickCount64();
        if (missReports < 12 && now - lastMiss > 2000) {
            ++missReports; lastMiss = now;
            log("ToyStore: vehicle %08X low on water at (%.1f, %.1f, %.1f), water %.2f, outside the water triggers "
                "(last (%.1f, %.1f, %.1f)-(%.1f, %.1f, %.1f)).", handle, at[0], at[1], at[2], water,
                nearest[0], nearest[1], nearest[2], nearest[3], nearest[4], nearest[5]);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
bool hook(std::uintptr_t base, unsigned rva, void* detour, void** original) noexcept {
    auto* target = reinterpret_cast<void*>(base + rva);
    if (MH_CreateHook(target, detour, original) != MH_OK) return false;
    if (MH_EnableHook(target) == MH_OK) return true;
    MH_RemoveHook(target); *original = nullptr; return false;
}
}

bool installToyStoreWater(std::uintptr_t base) {
    if (installedBase) return installedBase == base;
    const auto slot = [&](unsigned index) { return *reinterpret_cast<const std::uint32_t*>(base + triggerVtableRva + index * 4); };
    if (slot(10) != base + initializeRva || slot(42) != base + acceptRva ||
        *reinterpret_cast<const std::uint32_t*>(base + frameSlotRva) != base + frameRva ||
        !codeMatches(base + initializeRva, "51 56 8B F1 E8 ?? ?? ?? ?? 0F B7 0D ?? ?? ?? ?? 8B 46 04 8B 40 04") ||
        !codeMatches(base + acceptRva, "56 57 8B 7C 24 0C 57 8B F1 E8 ?? ?? ?? ?? 84 C0 74 08 57 8B CE E8 ?? ?? ?? ?? 5F 5E C2 04 00") ||
        !codeMatches(base + frameRva, "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00") ||
        !codeMatches(base + frameRva + 0x58, "8B 86 E4 00 00 00") ||
        !codeMatches(base + actorRva, "83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D") ||
        !codeMatches(base + componentRva, "8B 89 F0 00 00 00 85 C9 74 05 E9") ||
        !codeMatches(base + dataRva, "8B 54 24 08 8B 0D ?? ?? ?? ?? 8B 01 8B 40 10 6A 00 52") ||
        !codeMatches(base + oceanRva, "A1 ?? ?? ?? ?? C3") ||
        !codeMatches(base + heightRva, "51 8B 54 24 10 F3 0F 10 42 04 56 51 8B F1") ||
        !codeMatches(base + killRva, "83 EC 20 53 55 56 57 8B 7C 24 38 8B F1 83 FF 0F") ||
        // The script bindings: ActorGetName, ActorGetCollisionExtends, GetOceanHeightAtPos and Kill.
        !codeMatches(base + 0x32015C, "0F B7 0D ?? ?? ?? ?? 8B 50 04 51 52") ||
        *reinterpret_cast<const std::uint32_t*>(base + 0x32015F) != base + nameTypeRva ||
        !callsTo(base, 0x320168, dataRva) || !codeMatches(base + 0x320174, "8B 40 08") ||
        !codeMatches(base + 0x32008A, "0F B7 0D ?? ?? ?? ?? 8B 40 04 56 51 50") ||
        *reinterpret_cast<const std::uint32_t*>(base + 0x32008D) != base + collisionTypeRva ||
        !callsTo(base, 0x320097, dataRva) || !callsTo(base, 0x3200A7, collisionObjectRva) ||
        !codeMatches(base + 0x3200AC, "8B 10 8B 52 1C 8D 4C 24 20 51") ||
        !callsTo(base, 0x31D9AF, oceanRva) || !callsTo(base, 0x31D9F5, heightRva) || !callsTo(base, 0xAD6DD8, killRva) ||
        !callsTo(base, 0x32604A, 0x23EA30) ||
        !codeMatches(base + 0x23EA30, "8B 44 24 08 8B 00 85 C0 75 03 C2 08 00") ||
        !codeMatches(base + 0x3264DD, "66 8B 4E 06 BA FF 03 00 00 66 23 CA 66 3B 0D") ||
        !codeMatches(base + 0x3264FF, "8D 86 0C 02 00 00 EB 03 8D 46 2C")) return false;
    actorManager = *reinterpret_cast<void***>(base + 0x32603D);
    positionType = *reinterpret_cast<const std::uint16_t* const*>(base + 0x3264EC);
    if (!readable(actorManager, sizeof(void*)) || !readable(positionType, sizeof(*positionType))) return false;
    actorData = reinterpret_cast<ActorData>(base + 0x23EA30);
    actorFromHandle = reinterpret_cast<ActorFromHandle>(base + actorRva);
    component = reinterpret_cast<Component>(base + componentRva);
    data = reinterpret_cast<Data>(base + dataRva);
    collisionObject = reinterpret_cast<CollisionObject>(base + collisionObjectRva);
    ocean = reinterpret_cast<Ocean>(base + oceanRva);
    height = reinterpret_cast<Height>(base + heightRva);
    kill = reinterpret_cast<Kill>(base + killRva);
    nameType = reinterpret_cast<const std::uint16_t*>(base + nameTypeRva);
    collisionType = reinterpret_cast<const std::uint16_t*>(base + collisionTypeRva);
    triggers = {}; sunk = {}; reports = boundReports = missReports = 0;
    if (!hook(base, initializeRva, reinterpret_cast<void*>(&triggerInitialize), reinterpret_cast<void**>(&originalInitialize)))
        return false;
    installedBase = base;
    if (!hook(base, acceptRva, reinterpret_cast<void*>(&acceptObject), reinterpret_cast<void**>(&originalAccept)) ||
        !hook(base, frameRva, reinterpret_cast<void*>(&vehicleFrame), reinterpret_cast<void**>(&originalFrame))) {
        removeToyStoreWater(); return false;
    }
    return true;
}
void removeToyStoreWater() noexcept {
    if (!installedBase) return;
    for (const auto rva : {initializeRva, acceptRva, frameRva}) {
        auto* target = reinterpret_cast<void*>(installedBase + rva);
        MH_DisableHook(target); MH_RemoveHook(target);
    }
    installedBase = 0; originalInitialize = nullptr; originalAccept = nullptr; originalFrame = nullptr;
}
}
