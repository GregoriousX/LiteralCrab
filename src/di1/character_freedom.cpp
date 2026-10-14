// SPDX-License-Identifier: GPL-3.0-only
// [Gameplay] CharactersInAnyPlayset: play any character in any play set.
//
// Based on InfinityUnlocked by Philip Parkinson II, which patches the executable and replaces
// three data archives. This version changes admission, actor selection and tool FX in memory:
//
// GameManager::FindPlaysetForCharacter (0x48CB50) returns a character's home play set. The
// sites below compare it with the current world and reject a mismatch; each rejection branch
// is removed. The executable is never written and nothing changes while the option is off.
//   - System_IsCharacterValid, the scripts' gate: empty or different play set.
//   - Figure placement on the base: its play set validation block is skipped, as the game
//     already does in the Toy Box.
//   - Player character validity for the world: empty or different play set.
//   - Character swaps retain the native comparison and resource reload decision.
//   - Zone entry: the character counts as valid, as in the Toy Box.
//   - Actor lists: a play set loads every play set's actor list, which the Toy Box always does
//     ("IsHolodecksPlayset"), so other characters' actors exist there.
// Left native on purpose: IsCarCharacter (InfinityUnlocked makes every character a car), the
// spawn sparkle choice, the PlayableAvatars list parser and the "go to play set" dialog.
//
// PlayableAvatars stays original: it is exposed to systems beyond admission. A separate
// ownership map selects the Toy Box actor for visitors and keeps the native actor at home.
// The zone patch enables Toy Box FX resources for tools brought into play sets.
#include "di1/character_freedom.hpp"
#include "di1/character_freedom_policy.hpp"
#include "di1/character_freedom_data.hpp"
#include "di1/character_particles.hpp"
#include "minhook/MinHook.h"
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <intrin.h>

namespace crabe::di1 {
namespace {
    constexpr std::uint32_t findPlaysetRva = 0x8CB50;
    bool active{};
    std::uintptr_t installedBase{};
    constexpr std::uint32_t avatarVariantRva = 0xB01FC0;
    using AvatarVariant = void(__thiscall*)(void*, const char**);
    using ZoneString = const char*(__thiscall*)(void*, const char*, int, const char*, const char*);
    AvatarVariant originalVariant{};
    ZoneString zoneString{};
    void** zoneManager{};
    using ZoneBool = bool(__thiscall*)(void*, const char*, int, void*, bool);
    ZoneBool zoneBool{};
    bool trace{};
    unsigned experienceMultiplier = 1;
    volatile LONG variantEvents{}, experienceEvents{};
    constexpr std::uint32_t awardExperienceRva = 0x759D60;
    using AwardExperience = void(__thiscall*)(void*, std::uint32_t, float, bool);
    using PlayerForActor = int(__thiscall*)(void*, std::uint32_t);
    using ActorStats = void*(__thiscall*)(void*, std::uint32_t);
    using FindStat = void*(__thiscall*)(void*, int);
    using ReadStat = int(__thiscall*)(void*, int, void*);
    AwardExperience originalAward{};
    PlayerForActor playerForActor{};
    ActorStats actorStats{};
    FindStat findStat{};
    ReadStat readStat{};
    void **players{}, **statistics{}, **currency{};
    bool awardTraceInstalled{};
    constexpr std::uint32_t toolRuleRva = 0x79CA60, modelRequestRva = 0xBCD2D0, toolInitializeRva = 0xC1EEE0;
    using ToolRule = void(__thiscall*)(void*, void*);
    using ModelRequest = bool(__thiscall*)(void*, const char*, std::uint32_t, std::uint32_t);
    using ToolName = const char*(__thiscall*)(void*, const char*);
    using ToolInitialize = void(__thiscall*)(void*);
    ToolRule originalToolRule{};
    ModelRequest originalModelRequest{};
    ToolName currentTool{}, defaultTool{};
    ToolInitialize originalToolInitialize{};
    bool toolRuleTraceInstalled{}, modelTraceInstalled{}, toolInitializeTraceInstalled{};
    volatile LONG toolRuleEvents{}, modelEvents{}, toolInitializeEvents{};
    constexpr std::uint32_t controlSetRva = 0xB30000, toolActionRva = 0xC1E1C0;
    using ControlSet = void(__thiscall*)(void*, const char**, std::uint32_t);
    using ControlGet = bool(__thiscall*)(void*, const char**);
    using ActorData = void*(__cdecl*)(std::uint32_t, std::uint32_t);
    using ToolAction = void(__thiscall*)(void*, const char*, const char*, std::uint32_t, std::uint32_t);
    ControlSet originalControlSet{};
    ControlGet controlGet{};
    ActorData actorData{};
    const std::uint16_t* stateDataType{};
    ToolAction originalToolAction{};
    bool controlSetInstalled{}, actionTraceInstalled{};
    constexpr std::uint32_t controlGetRva = 0xB30090, toolWorldRva = 0xC1D200;
    using ToolWorld = int(__thiscall*)(void*, const void*);
    ControlGet originalControlGet{};
    ToolWorld originalToolWorld{};
    bool menuFreedomInstalled{}, toolWorldInstalled{};
    volatile LONG toolFreedomEvents{};
    constexpr std::uint32_t toolAssetRva = 0x2E6130;
    using ToolAsset = bool(__thiscall*)(void*, const char*, std::uint32_t);
    ToolAsset originalToolAsset{};
    bool toolAssetTraceInstalled{};
    volatile LONG toolAssetEvents{};
    constexpr std::uint32_t toolUnlockedRva = 0xC1A600, projectileCreateRva = 0x614E00;
    using ToolUnlocked = bool(__thiscall*)(void*, const char*);
    struct ProjectileVector { float x, y, z; };
    static_assert(sizeof(ProjectileVector) == 12);
    using ProjectileCreate = void*(__cdecl*)(std::uint32_t, ProjectileVector, std::uint32_t, void*);
    ToolUnlocked originalToolUnlocked{};
    ProjectileCreate originalProjectileCreate{};
    bool toolUnlockedInstalled{}, projectileTraceInstalled{};
    using ToolRefresh = void(__thiscall*)(void*, bool);
    ToolRefresh refreshTool{};
    thread_local bool refreshingToolAvailability{};
    volatile LONG toolUnlockedEvents{}, toolCommandEvents{}, toolScriptEvents{}, projectileEvents{};
    constexpr std::uint32_t emitterLookupRva = 0x2D8AC0;
    using EmitterLookup = void*(__thiscall*)(void*, std::uint32_t);
    EmitterLookup originalEmitterLookup{};
    bool emitterInstalled{};
    SRWLOCK emitterTraceLock = SRWLOCK_INIT;
    struct EmitterTraceKey { std::uint32_t id; bool available; };
    EmitterTraceKey emitterTraceKeys[256]{};
    unsigned emitterTraceCount{};
    volatile LONG controlEvents{}, actionEvents{};
    constexpr const char* toolContexts[] = {
        "Allow_UseItems", "Allow_EquipNThrow", "TUT_DISABLE_USE_ITEM", "TUT_DISABLE_SELECT_ITEM",
        "SPECTATOR_DISABLE", "DisableToolSelection", "TardisDisableToolSelection", "racemission",
        "IGP_Active", "_cycleTools", "_cycleItems", "_cycleProps", "StickyHandOut", "InCarry",
        "InUpperBodyMove", "InTeleport", "HeldByZPE"
    };
    void traceToolContexts(std::uint32_t actor, const char* stage) noexcept;
    void traceToolState(void* tools, const char* stage, const char* requested = nullptr) noexcept;
    std::uint32_t componentActor(void* component) noexcept;
    bool playerInPlayset(std::uint32_t actor) noexcept;
    template<std::size_t N> void copyTraceString(char (&out)[N], const char* value) noexcept;
    bool __fastcall traceToolAsset(void* self, void*, const char* name, std::uint32_t type) {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        char copy[128]; copyTraceString(copy, name);
        const bool report = type == 2 && !_strnicmp(copy, "Tool_", 5) && InterlockedIncrement(&toolAssetEvents) <= 256;
        const bool result = originalToolAsset(self, name, type);
        if (report) log("Character trace: tool asset ready name=%s type=%u available=%d callerRVA=%08X",
            copy, type, result, caller >= installedBase && caller - installedBase <= 0x1511000 ?
                static_cast<unsigned>(caller - installedBase) : 0u);
        return result;
    }

    std::uint32_t traceActorHandle(void* actor) noexcept {
        __try { return actor ? *reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(actor) + 4) : 0; }
        __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    void* zoneBlockedTool(void* tools, const char* name) noexcept {
        if (!tools || !name) return nullptr;
        __try {
            const auto* memory = static_cast<const unsigned char*>(tools);
            const auto count = *reinterpret_cast<const unsigned*>(memory + 0x2C0);
            auto* const* entries = *reinterpret_cast<void* const* const*>(memory + 0x2BC);
            if (!entries || count > 512) return nullptr;
            for (unsigned i = 0; i < count; ++i) {
                auto* entry = static_cast<unsigned char*>(entries[i]);
                if (!entry || (entry[0x78] & 5) ||
                    !*reinterpret_cast<const void* const*>(entry + 0x24) ||
                    *reinterpret_cast<const int*>(entry + 0x70) != 0) continue;
                const auto* toolName = *reinterpret_cast<const char* const*>(entry + 0x14);
                if (toolName && !_stricmp(toolName, name)) return entry;
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return nullptr;
    }
    bool __fastcall allowToolUnlocked(void* self, void*, const char* name) {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        // The successful Toy Box throw uses a different path from the upper-body
        // callback. Keep independent budgets for that callback, script and command.
        const bool upperBody = caller >= installedBase + 0x8B6140 && caller < installedBase + 0x8B6697;
        const bool command = caller == installedBase + 0x695854;
        const bool script = caller == installedBase + 0x5FFBF0;
        const bool report = trace && ((upperBody && InterlockedIncrement(&toolUnlockedEvents) <= 96) ||
            (command && InterlockedIncrement(&toolCommandEvents) <= 96) ||
            (script && InterlockedIncrement(&toolScriptEvents) <= 96));
        char copy[96]{};
        if (report) copyTraceString(copy, name);
        bool result = originalToolUnlocked(self, name);
        if (!result && active && !refreshingToolAvailability && refreshTool && playerInPlayset(componentActor(self))) {
            if (void* entry = zoneBlockedTool(self, name)) {
                // Initial inventory construction can precede player registration.
                // Re-run the engine's GetTool(false) checks after that registration;
                // its unlock/upgrade checks still decide whether the tool is usable.
                refreshingToolAvailability = true;
                refreshTool(entry, false);
                refreshingToolAvailability = false;
                result = originalToolUnlocked(self, name);
                if (trace && InterlockedIncrement(&toolFreedomEvents) <= 64)
                    log("Character freedom: player tool availability recalculated actor=%08X unlocked=%d",
                        componentActor(self), result);
            }
        }
        if (report) {
            log("Character trace: tool availability query source=%s name=%s unlocked=%d callerRVA=%08X",
                upperBody ? "upper_body" : command ? "command" : "script", copy, result,
                static_cast<unsigned>(caller - installedBase));
            traceToolState(self, "auto-equip", copy);
        }
        return result;
    }
    void* __cdecl traceProjectileCreate(std::uint32_t requestedActor, ProjectileVector position, std::uint32_t kind, void* owner) {
        const bool report = InterlockedIncrement(&projectileEvents) <= 128;
        const auto ownerHandle = report ? traceActorHandle(owner) : 0;
        void* result = originalProjectileCreate(requestedActor, position, kind, owner);
        if (report) log("Character trace: projectile create owner=%08X kind=%08X requested=%08X actor=%08X created=%d",
            ownerHandle, kind, requestedActor, traceActorHandle(result), result != nullptr);
        return result;
    }
    void* __fastcall lookupCharacterEmitter(void* self, void*, std::uint32_t id) {
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        void* result = originalEmitterLookup(self, id);
        // Emitter initialization queries this registry. Deduplicate by ID/result
        // so repeated footsteps/ambient effects cannot consume the whole budget.
        if (trace && caller == installedBase + 0x2EF8E2) {
            bool report = true;
            AcquireSRWLockExclusive(&emitterTraceLock);
            for (unsigned i = 0; i < emitterTraceCount; ++i)
                if (emitterTraceKeys[i].id == id && emitterTraceKeys[i].available == (result != nullptr)) report = false;
            if (emitterTraceCount == 256) report = false;
            if (report) emitterTraceKeys[emitterTraceCount++] = {id, result != nullptr};
            ReleaseSRWLockExclusive(&emitterTraceLock);
            if (report) log("Character trace: emitter definition id=%08X available=%d", id, result != nullptr);
        }
        return result;
    }

    bool currentWorldIsPlayset() noexcept {
        if (!zoneManager || !zoneString) return false;
        __try {
            if (!*zoneManager) return false;
            const auto zone = *reinterpret_cast<const int*>(static_cast<const unsigned char*>(*zoneManager) + 0x154);
            const auto* playset = zoneString(*zoneManager, "PlaysetProperties", zone, nullptr, nullptr);
            if (!playset) return false;
            for (const auto* name : {"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"})
                if (!_stricmp(playset, name)) return true;
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
    bool playerInPlayset(std::uint32_t actor) noexcept {
        if (!actor || !players || !playerForActor) return false;
        __try {
            if (!*players) return false;
            const auto player = playerForActor(*players, actor);
            return player >= 0 && player < 4 && currentWorldIsPlayset();
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool canSelectPlayerTools(void* state, std::uint32_t actor) noexcept {
        if (!playerInPlayset(actor) || !originalControlGet) return false;
        __try {
            const char* useGate = "TUT_DISABLE_USE_ITEM";
            const char* spectatorGate = "SPECTATOR_DISABLE";
            return !originalControlGet(state, &useGate) && !originalControlGet(state, &spectatorGate);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    bool __fastcall allowToolMenu(void* self, void*, const char** context) {
        const bool result = originalControlGet(self, context);
        if (!result || !active) return result;
        bool allow = false;
        std::uint32_t actor{};
        __try {
            // Read-side coverage for native bulk/prefix context assignments, while
            // retaining all other hardware/action/frontend restrictions.
            if (context && *context && !std::strcmp(*context, "TUT_DISABLE_SELECT_ITEM")) {
                actor = *static_cast<const std::uint32_t*>(self);
                allow = canSelectPlayerTools(self, actor);
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (allow && trace && InterlockedIncrement(&toolFreedomEvents) <= 64)
            log("Character freedom: playset tool menu gate bypassed for player actor=%08X", actor);
        return allow ? false : result;
    }
    int __fastcall allowToolWorld(void* self, void*, const void* zones) {
        const int result = originalToolWorld(self, zones);
        if (result || !active || !zones) return result;
        std::uint32_t actor{};
        __try {
            const auto* memory = static_cast<const unsigned char*>(self);
            if ((memory[0x78] & 5) != 4) return result; // Keep locked and suppressed equipment unavailable.
            auto* tools = *static_cast<void**>(self);
            // The native caller checks upgrade/unlock state before this zone filter.
            // Do not change the cached native result, award bits, rules or suppression.
            const auto* owner = *reinterpret_cast<const unsigned char* const*>(static_cast<unsigned char*>(tools) + 4);
            if (owner) actor = *reinterpret_cast<const std::uint32_t*>(owner + 4);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        if (!playerInPlayset(actor)) return result;
        if (trace && InterlockedIncrement(&toolFreedomEvents) <= 64)
            log("Character freedom: unlocked player tool allowed across playset zone filter actor=%08X", actor);
        return 1;
    }
    mods::LuaApi lua{};
    CharacterPlaysets ownership;
    SRWLOCK ownershipLock = SRWLOCK_INIT;

    int __cdecl registerPlaysetAvatars(lua_State* state) {
        std::size_t nameSize{}, listSize{};
        const auto* name = lua.toLString(state, 1, &nameSize);
        const auto* list = lua.toLString(state, 2, &listSize);
        if (!name || !list || !nameSize || nameSize > 64 || listSize > 8192 ||
            std::memchr(name, '\0', nameSize) || std::memchr(list, '\0', listSize)) return 0;
        AcquireSRWLockExclusive(&ownershipLock);
        try { ownership.add({name, nameSize}, {list, listSize}); }
        catch (...) { ReleaseSRWLockExclusive(&ownershipLock); return 0; }
        ReleaseSRWLockExclusive(&ownershipLock);
        return 0;
    }
    int __cdecl isRegisteredAvatar(lua_State* state) {
        std::size_t size{};
        const auto* name = lua.toLString(state, 1, &size);
        bool known = false;
        if (name && size <= 256 && !std::memchr(name, '\0', size)) {
            AcquireSRWLockShared(&ownershipLock);
            try { known = ownership.registered({name, size}); } catch (...) {}
            ReleaseSRWLockShared(&ownershipLock);
        }
        lua.pushBoolean(state, known);
        return 1;
    }

    void __fastcall chooseAvatarVariant(void* self, void*, const char** avatar) {
        // Called during creation/recreation, including a return from a vehicle; no frame work.
        bool visitor = false;
        const char* current = nullptr;
        int zone = -1;
        if (avatar && *avatar && zoneManager && *zoneManager) {
            const auto* manager = static_cast<const unsigned char*>(*zoneManager);
            zone = *reinterpret_cast<const int*>(manager + 0x154);
            current = zoneString(*zoneManager, "PlaysetProperties", zone, nullptr, nullptr);
            if (current) {
                AcquireSRWLockShared(&ownershipLock);
                try { visitor = ownership.foreign(*avatar, current); }
                catch (...) { visitor = false; }
                ReleaseSRWLockShared(&ownershipLock);
            }
        }
        char input[128]{};
        const bool report = trace && InterlockedIncrement(&variantEvents) <= 64;
        if (report && avatar && *avatar) strncpy_s(input, *avatar, _TRUNCATE);
        // Leaving the engine string intact selects the already registered Toy Box actor.
        if (!visitor) originalVariant(self, avatar);
        if (report) {
            const bool haveWorld = zoneBool && zoneManager && *zoneManager;
            log("Character trace: avatar=%s selected=%s playset=%s visitor=%d ToyBoxFX=%d BloatFX=%d",
                input, avatar && *avatar ? *avatar : "?", current ? current : "?", visitor,
                haveWorld && zoneBool(*zoneManager, "WantToyBoxFX", zone, nullptr, false),
                haveWorld && zoneBool(*zoneManager, "WantBloatFX", zone, nullptr, false));
        }
    }

    void __fastcall traceExperience(void* self, void*, std::uint32_t actor, float amount, bool tracked) {
        // The explicit test option scales existing positive awards; never synthesize one.
        const auto originalAmount = amount;
        if (amount > 0 && std::isfinite(amount)) {
            const auto scaled = amount * experienceMultiplier;
            if (std::isfinite(scaled)) amount = scaled;
        }
        if (!trace || InterlockedIncrement(&experienceEvents) > 128) { originalAward(self, actor, amount, tracked); return; }
        const auto player = players && *players ? playerForActor(*players, actor) : -1;
        auto* owner = statistics && *statistics ? actorStats(*statistics, actor) : nullptr;
        const bool haveOwner = readable(owner, 0x18);
        const bool haveXp = haveOwner && findStat(owner, 0xAD);
        const bool haveLevel = haveOwner && findStat(owner, 0x13E);
        const auto xp = haveXp ? readStat(*statistics, 0xAD, owner) : -1;
        const auto level = haveLevel ? readStat(*statistics, 0x13E, owner) : -1;
        const auto scope = haveOwner ? *reinterpret_cast<const int*>(static_cast<const unsigned char*>(owner) + 4) : -1;
        float multiplier = -1;
        if (currency && *currency && player >= 0 && player < 4) {
            const auto* value = static_cast<const unsigned char*>(*currency) + player * 24 + 0xC;
            if (readable(value, sizeof(float))) multiplier = *reinterpret_cast<const float*>(value);
        }
        log("Character trace: EXP request actor=%08X player=%d amount=%.3f tracked=%d scope=%d XPstat=%d LEVELstat=%d XP=%d level=%d multiplier=%.3f originalAmount=%.3f testMultiplier=%u",
            actor, player, amount, tracked, scope, haveXp, haveLevel, xp, level, multiplier, originalAmount, experienceMultiplier);
        originalAward(self, actor, amount, tracked);
        owner = statistics && *statistics ? actorStats(*statistics, actor) : nullptr;
        const bool resultOwner = readable(owner, 0x18);
        log("Character trace: EXP result actor=%08X XP=%d level=%d",
            actor, resultOwner && findStat(owner, 0xAD) ? readStat(*statistics, 0xAD, owner) : -1,
            resultOwner && findStat(owner, 0x13E) ? readStat(*statistics, 0x13E, owner) : -1);
        if (controlSetInstalled) traceToolContexts(actor, "orb award");
    }

    bool installExperienceTrace(std::uintptr_t base) {
        if (!codeMatches(base + awardExperienceRva,
            "8B 44 24 04 56 57 8B F1 8B 0D ?? ?? ?? ?? 50 E8") ||
            !callsTo(base, 0x759D6F, 0xAE8070) || !callsTo(base, 0x759DA5, 0xB7B450) ||
            !codeMatches(base + 0xB7B450, "8B 44 24 04 50 81 C1 3C 01 00 00 E8") ||
            !codeMatches(base + 0xB85D50, "8B 49 14 33 C0 85 C9 74 05 E9") ||
            !codeMatches(base + 0xB88B40, "8B 4C 24 08 85 C9 74 1A 8B 44 24 04 50 E8") ||
            !callsTo(base, 0xB88B4D, 0xB85D50) ||
            !codeMatches(base + 0x759DCF, "A1 ?? ?? ?? ?? 8D 14 7F F3 0F 10 44 D0 0C")) return false;
        players = *reinterpret_cast<void***>(base + 0x759D6A);
        statistics = *reinterpret_cast<void***>(base + 0x759DA1);
        currency = *reinterpret_cast<void***>(base + 0x759DD0);
        if (!readable(players, sizeof(void*)) || !readable(statistics, sizeof(void*)) || !readable(currency, sizeof(void*))) return false;
        playerForActor = reinterpret_cast<PlayerForActor>(base + 0xAE8070);
        actorStats = reinterpret_cast<ActorStats>(base + 0xB7B450);
        findStat = reinterpret_cast<FindStat>(base + 0xB85D50);
        readStat = reinterpret_cast<ReadStat>(base + 0xB88B40);
        auto* target = reinterpret_cast<void*>(base + awardExperienceRva);
        void* trampoline{};
        if (MH_CreateHook(target, reinterpret_cast<void*>(&traceExperience), &trampoline) != MH_OK) return false;
        originalAward = reinterpret_cast<AwardExperience>(trampoline);
        if (MH_EnableHook(target) != MH_OK) { MH_RemoveHook(target); return false; }
        awardTraceInstalled = true;
        return true;
    }

    // Guard diagnostic reads separately so an unavailable object never changes gameplay.
    template<std::size_t N> void copyTraceString(char (&out)[N], const char* value) noexcept {
        strcpy_s(out, "<none>");
        if (!value) return;
        __try {
            std::size_t i = 0;
            for (; i + 1 < N && value[i]; ++i) out[i] = value[i];
            out[i] = 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(out, "<unreadable>"); }
    }
    std::uint32_t componentActor(void* component) noexcept {
        __try {
            const auto* owner = *reinterpret_cast<const unsigned char* const*>(static_cast<const unsigned char*>(component) + 4);
            return owner ? *reinterpret_cast<const std::uint32_t*>(owner + 4) : 0;
        } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
    }
    void traceToolContexts(std::uint32_t actor, const char* stage) noexcept {
        if (!actor || !controlGet || !actorData || !stateDataType) return;
        __try {
            auto* data = actorData(actor, *stateDataType);
            if (!data) return;
            // A local borrowed Genie string reference is sufficient: native getters do
            // not consume it, and the dictionary itself interns keys for comparison.
            char values[sizeof(toolContexts) / sizeof(*toolContexts) + 1]{};
            for (std::size_t i = 0; i < sizeof(values) - 1; ++i) {
                const char* key = toolContexts[i];
                values[i] = controlGet(data, &key) ? '1' : '0';
            }
            log("Character trace: tool contexts %s actor=%08X values=%s", stage, actor, values);
        } __except (EXCEPTION_EXECUTE_HANDLER) { log("Character trace: tool contexts %s unavailable", stage); }
    }
    bool watchedToolContext(const char* name) noexcept {
        for (const auto* key : toolContexts) if (!std::strcmp(name, key)) return true;
        return false;
    }
    void __fastcall setPlayerToolContext(void* self, void*, const char** context, std::uint32_t value) {
        const auto requested = value;
        const auto caller = reinterpret_cast<std::uintptr_t>(_ReturnAddress());
        char name[96];
        __try { copyTraceString(name, context ? *context : nullptr); }
        __except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(name, "<unreadable>"); }
        std::uint32_t actor{};
        const bool selection = !std::strcmp(name, "TUT_DISABLE_SELECT_ITEM");
        const bool report = trace && watchedToolContext(name) && InterlockedIncrement(&controlEvents) <= 512;
        if (report || (active && selection && (value & 0xFF))) {
            __try { actor = *static_cast<const std::uint32_t*>(self); }
            __except (EXCEPTION_EXECUTE_HANDLER) {}
        }
        // Compiled input expressions can read the context dictionary directly.
        // Normalize only the native bool byte of this one progression gate, even
        // with trace off or exhausted. Keep the caller's reference and other bits.
        if (active && selection && (value & 0xFF) && canSelectPlayerTools(self, actor)) value &= ~0xFFu;
        originalControlSet(self, context, value);
        if (report) log("Character trace: control set actor=%08X context=%s value=%u requested=%u callerRVA=%08X",
            actor, name, value, requested, caller >= installedBase && caller - installedBase <= 0x1511000 ?
                static_cast<unsigned>(caller - installedBase) : 0u);
    }
    void __fastcall traceToolAction(void* self, void*, const char* group, const char* action,
        std::uint32_t first, std::uint32_t second) {
        const bool report = InterlockedIncrement(&actionEvents) <= 96;
        if (report) {
            char groupName[96], actionName[96];
            copyTraceString(groupName, group); copyTraceString(actionName, action);
            log("Character trace: tool action actor=%08X group=%s action=%s flags=%u,%u",
                componentActor(self), groupName, actionName, first, second);
            traceToolState(self, "action");
            traceToolContexts(componentActor(self), "action");
        }
        originalToolAction(self, group, action, first, second);
        if (report) traceToolState(self, "after action");
    }
    void traceToolState(void* tools, const char* stage, const char* requested) noexcept {
        if (!tools) return;
        __try {
            char item[96], prop[96], itemDefault[96], propDefault[96];
            copyTraceString(item, currentTool(tools, "Items"));
            copyTraceString(prop, currentTool(tools, "Props"));
            copyTraceString(itemDefault, defaultTool(tools, "Items"));
            copyTraceString(propDefault, defaultTool(tools, "Props"));
            log("Character trace: tools %s actor=%08X item=%s prop=%s defaultItem=%s defaultProp=%s",
                stage, componentActor(tools), item, prop, itemDefault, propDefault);
            // Include selected/default equipment and the queried auto-equip candidate.
            // A character's default item can differ from its throwable tool.
            const auto* memory = static_cast<const unsigned char*>(tools);
            const auto count = *reinterpret_cast<const unsigned*>(memory + 0x2C0);
            auto* const* entries = *reinterpret_cast<void* const* const*>(memory + 0x2BC);
            if (count > 512 || !entries) return;
            for (unsigned i = 0; i < count; ++i) {
                const auto* entry = static_cast<const unsigned char*>(entries[i]);
                if (!entry) continue;
                char name[96];
                copyTraceString(name, *reinterpret_cast<const char* const*>(entry + 0x14));
                if (std::strcmp(name, itemDefault) && std::strcmp(name, propDefault) &&
                    std::strcmp(name, item) && std::strcmp(name, prop) &&
                    (!requested || std::strcmp(name, requested))) continue;
                const auto flags = entry[0x78];
                log("Character trace: tool entry %s %s suppressed=%d awarded=%d unlocked=%d flags=%02X zoneFilter=%d",
                    stage, name, bool(flags & 1), bool(flags & 2), bool(flags & 4), unsigned(flags),
                    *reinterpret_cast<const void* const*>(entry + 0x24) != nullptr);
                const auto children = *reinterpret_cast<const unsigned*>(entry + 8);
                auto* const* childList = *reinterpret_cast<void* const* const*>(entry + 4);
                if (!childList || children > 64) continue;
                for (unsigned j = 0; j < children; ++j) {
                    const auto* child = static_cast<const unsigned char*>(childList[j]);
                    if (!child) continue;
                    char actorName[96], fallback[96];
                    copyTraceString(actorName, *reinterpret_cast<const char* const*>(child + 0xC));
                    copyTraceString(fallback, *reinterpret_cast<const char* const*>(child + 0x10));
                    log("Character trace: tool child %s parent=%s actor=%s fallback=%s handle=%08X pending=%d",
                        stage, name, actorName, fallback, *reinterpret_cast<const unsigned*>(child + 0x5C), bool(child[0x64]));
                }
            }
        } __except (EXCEPTION_EXECUTE_HANDLER) { log("Character trace: tools %s unavailable", stage); }
    }
    void __fastcall traceToolRule(void* self, void*, void* tools) {
        const bool report = InterlockedIncrement(&toolRuleEvents) <= 64;
        if (report) {
            __try {
                const auto* rule = static_cast<const unsigned char*>(self);
                char item[96], prop[96];
                copyTraceString(item, *reinterpret_cast<const char* const*>(rule + 0x14));
                copyTraceString(prop, *reinterpret_cast<const char* const*>(rule + 0x18));
                const auto count = *reinterpret_cast<const unsigned*>(rule + 0x2C);
                log("Character trace: tool rule actor=%08X flags=%02X item=%s prop=%s allowed=%u",
                    componentActor(tools), rule[0x34], item, prop, count);
                auto* const* allowed = *reinterpret_cast<const char* const* const*>(rule + 0x28);
                if (allowed && count <= 128) for (unsigned i = 0; i < count; ++i) {
                    char name[96]; copyTraceString(name, allowed[i]);
                    log("Character trace: tool rule allows %s", name);
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) { log("Character trace: tool rule unavailable"); }
            traceToolState(tools, "before rule");
        }
        originalToolRule(self, tools);
        if (report) traceToolState(tools, "after rule");
    }
    void __fastcall traceToolInitialize(void* self, void*) {
        originalToolInitialize(self);
        if (InterlockedIncrement(&toolInitializeEvents) <= 64) {
            traceToolState(self, "initialized");
            traceToolContexts(componentActor(self), "initialized");
        }
    }
    bool __fastcall traceModelRequest(void* self, void*, const char* model, std::uint32_t first, std::uint32_t second) {
        char name[128];
        copyTraceString(name, model);
        // NPC customization can run before the first avatar; do not spend its budget.
        const bool report = !_strnicmp(name, "Avatar_", 7) && InterlockedIncrement(&modelEvents) <= 96;
        // Native Genie string argument is passed through once; its native destruction
        // remains inside the original method. Never read it after that call.
        const auto actor = report ? componentActor(self) : 0;
        const bool applied = originalModelRequest(self, model, first, second);
        if (report) log("Character trace: model request actor=%08X model=%s flags=%u,%u applied=%d",
            actor, name, first, second, applied);
        return applied;
    }
    void installToolModelTrace(std::uintptr_t base) {
        toolRuleEvents = modelEvents = toolInitializeEvents = 0;
        const bool toolsVerified = codeMatches(base + toolRuleRva,
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 20") &&
            callsTo(base, 0x7A9792, toolRuleRva) && callsTo(base, 0x79CA94, 0xC1A1C0) &&
            callsTo(base, 0x79CB58, 0xC1A870) &&
            codeMatches(base + 0xC1A1C0, "53 55 56 57 8B F9 33 ED 8D B7 EC 02 00 00") &&
            codeMatches(base + 0xC1A870, "8B 44 24 04 56 50 8B F1 E8") &&
            callsTo(base, 0xC1A878, 0xC19CC0);
        void* trampoline{};
        auto* target = reinterpret_cast<void*>(base + toolRuleRva);
        if (toolsVerified && MH_CreateHook(target, reinterpret_cast<void*>(&traceToolRule), &trampoline) == MH_OK) {
            currentTool = reinterpret_cast<ToolName>(base + 0xC1A1C0);
            defaultTool = reinterpret_cast<ToolName>(base + 0xC1A870);
            originalToolRule = reinterpret_cast<ToolRule>(trampoline);
            toolRuleTraceInstalled = MH_EnableHook(target) == MH_OK;
            if (!toolRuleTraceInstalled) MH_RemoveHook(target);
        }
        if (toolsVerified && codeMatches(base + toolInitializeRva, "83 EC 40 53 8B D9 0F B7 0D ?? ?? ?? ?? 8B 43 04") &&
            callsTo(base, 0xC1EF2F, 0x2CFAE0) && callsTo(base, 0xC1EF57, 0xC1EB50)) {
            target = reinterpret_cast<void*>(base + toolInitializeRva);
            if (MH_CreateHook(target, reinterpret_cast<void*>(&traceToolInitialize), &trampoline) == MH_OK) {
                originalToolInitialize = reinterpret_cast<ToolInitialize>(trampoline);
                toolInitializeTraceInstalled = MH_EnableHook(target) == MH_OK;
                if (!toolInitializeTraceInstalled) MH_RemoveHook(target);
            }
        }
        const bool modelVerified = codeMatches(base + modelRequestRva,
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 51 56 8B F1") &&
            callsTo(base, 0xBCDB96, modelRequestRva) && codeMatches(base + 0xBCD3FC, "C2 0C 00");
        target = reinterpret_cast<void*>(base + modelRequestRva);
        if (modelVerified && MH_CreateHook(target, reinterpret_cast<void*>(&traceModelRequest), &trampoline) == MH_OK) {
            originalModelRequest = reinterpret_cast<ModelRequest>(trampoline);
            modelTraceInstalled = MH_EnableHook(target) == MH_OK;
            if (!modelTraceInstalled) MH_RemoveHook(target);
        }
        log("Character trace: read-only tool rules=%d (64 events), model requests=%d (96 events), tool initialization=%d (64 events)",
            toolRuleTraceInstalled, modelTraceInstalled, toolInitializeTraceInstalled);
    }

    void installToolControlTrace(std::uintptr_t base) {
        controlEvents = actionEvents = 0;
        const bool verified = controlSetInstalled &&
            codeMatches(base + 0xB3007F, "C2 08 00") &&
            menuFreedomInstalled &&
            callsTo(base, 0xB3000F, 0x17D10) && callsTo(base, 0xB3009E, 0x17D10) &&
            codeMatches(base + 0x283E50, "8B 54 24 08 8B 0D ?? ?? ?? ?? 8B 01 8B 40 10") &&
            codeMatches(base + 0x83E0FB, "0F B7 0D ?? ?? ?? ?? 51 50 E8") &&
            callsTo(base, 0x83E104, 0x283E50) && callsTo(base, 0x83E127, 0xB30090) &&
            callsTo(base, 0x88A186, controlSetRva);
        void* trampoline{};
        auto* target = reinterpret_cast<void*>(base + controlSetRva);
        if (verified) {
            stateDataType = *reinterpret_cast<const std::uint16_t* const*>(base + 0x83E0FE);
            actorData = reinterpret_cast<ActorData>(base + 0x283E50);
            controlGet = originalControlGet; // Snapshot native stored state, before the menu exception.
        }
        const bool actionVerified = currentTool && defaultTool &&
            codeMatches(base + toolActionRva, "8B 44 24 04 56 50 8B F1 E8") &&
            callsTo(base, 0xC1E1C8, 0xC19CC0) && callsTo(base, 0x5FF80B, toolActionRva) &&
            callsTo(base, 0xC1E208, 0xC1DCB0) && codeMatches(base + 0xC1E20E, "C2 10 00");
        target = reinterpret_cast<void*>(base + toolActionRva);
        if (actionVerified && MH_CreateHook(target, reinterpret_cast<void*>(&traceToolAction), &trampoline) == MH_OK) {
            originalToolAction = reinterpret_cast<ToolAction>(trampoline);
            actionTraceInstalled = MH_EnableHook(target) == MH_OK;
            if (!actionTraceInstalled) MH_RemoveHook(target);
        }
        log("Character trace: control context writes=%d (512 events), tool actions=%d (96 events)",
            controlSetInstalled, actionTraceInstalled);
        if (controlSetInstalled) {
            log("Character trace: tool context values order=Allow_UseItems,Allow_EquipNThrow,TUT_DISABLE_USE_ITEM,TUT_DISABLE_SELECT_ITEM,SPECTATOR_DISABLE,DisableToolSelection,TardisDisableToolSelection,racemission,IGP_Active,_cycleTools,_cycleItems,_cycleProps,StickyHandOut,InCarry,InUpperBodyMove,InTeleport,HeldByZPE");
        }
        toolAssetEvents = 0;
        target = reinterpret_cast<void*>(base + toolAssetRva);
        if (codeMatches(base + toolAssetRva, "8B 44 24 08 8B 54 24 04 53 50 52 32 DB E8") &&
            callsTo(base, 0x2E613D, 0x2E60B0) && callsTo(base, 0xC1DF81, toolAssetRva) &&
            codeMatches(base + 0x2E614E, "C2 08 00") &&
            MH_CreateHook(target, reinterpret_cast<void*>(&traceToolAsset), &trampoline) == MH_OK) {
            originalToolAsset = reinterpret_cast<ToolAsset>(trampoline);
            toolAssetTraceInstalled = MH_EnableHook(target) == MH_OK;
            if (!toolAssetTraceInstalled) MH_RemoveHook(target);
        }
        log("Character trace: read-only tool actor asset readiness=%d (256 events)", toolAssetTraceInstalled);
        toolUnlockedEvents = toolCommandEvents = toolScriptEvents = projectileEvents = 0;
        target = reinterpret_cast<void*>(base + projectileCreateRva);
        if (codeMatches(base + projectileCreateRva, "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 83 EC 44") &&
            callsTo(base, 0x6151A8, projectileCreateRva) && callsTo(base, 0x614E3E, 0x60B1F0) &&
            codeMatches(base + 0x6151AD, "83 C4 18 33 C9 85 C0") &&
            MH_CreateHook(target, reinterpret_cast<void*>(&traceProjectileCreate), &trampoline) == MH_OK) {
            originalProjectileCreate = reinterpret_cast<ProjectileCreate>(trampoline);
            projectileTraceInstalled = MH_EnableHook(target) == MH_OK;
            if (!projectileTraceInstalled) MH_RemoveHook(target);
        }
        log("Character trace: tool availability queries=%d (96 per source), read-only projectile creation=%d (128 events)",
            toolUnlockedInstalled, projectileTraceInstalled);
        log("Character trace: emitter definitions=%d (256 unique ID/result pairs)", emitterInstalled);
    }

    bool installToolFreedom(std::uintptr_t base) {
        if (!codeMatches(base + controlSetRva, "53 56 57 8B 7C 24 10 8B 07 8B F1 8B 4E 20 50 E8") ||
            !callsTo(base, 0xB3000F, 0x17D10) || !codeMatches(base + 0xB3007F, "C2 08 00") ||
            !codeMatches(base + controlGetRva, "56 57 8B 7C 24 0C 8B 07 8B F1 8B 4E 20 50 E8") ||
            !callsTo(base, 0xB3009E, 0x17D10) || !codeMatches(base + 0xB300E3, "C2 04 00") ||
            !codeMatches(base + toolWorldRva, "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 56 57 8B F9") ||
            !callsTo(base, 0xC1E4C6, toolWorldRva) || !callsTo(base, 0xC1D248, 0xAF23B0) ||
            !codeMatches(base + 0xC1D2BC, "C2 04 00") ||
            !codeMatches(base + 0x759D60, "8B 44 24 04 56 57 8B F1 8B 0D ?? ?? ?? ?? 50 E8") ||
            !callsTo(base, 0x759D6F, 0xAE8070) ||
            !codeMatches(base + toolUnlockedRva, "8B 44 24 04 53 55 56 57 8B F9 8B 0D") ||
            !callsTo(base, 0x8B61D6, toolUnlockedRva) || !callsTo(base, 0x69584F, toolUnlockedRva) ||
            !callsTo(base, 0x5FFBEB, toolUnlockedRva) ||
            !codeMatches(base + 0xC1A64C, "8A 41 78 5F 5E C0 E8 02 5D 24 01 5B C2 04 00") ||
            !codeMatches(base + 0xC1E280, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 0C") ||
            !callsTo(base, 0xC1E6A0, 0xC1E280) || !codeMatches(base + 0xC1E631, "C2 04 00")) return false;
        players = *reinterpret_cast<void***>(base + 0x759D6A);
        if (!readable(players, sizeof(void*))) return false;
        playerForActor = reinterpret_cast<PlayerForActor>(base + 0xAE8070);
        void* trampoline{};
        auto* target = reinterpret_cast<void*>(base + controlGetRva);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&allowToolMenu), &trampoline) != MH_OK) return false;
        originalControlGet = reinterpret_cast<ControlGet>(trampoline);
        menuFreedomInstalled = MH_EnableHook(target) == MH_OK;
        if (!menuFreedomInstalled) { MH_RemoveHook(target); return false; }
        target = reinterpret_cast<void*>(base + toolWorldRva);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&allowToolWorld), &trampoline) != MH_OK) return false;
        originalToolWorld = reinterpret_cast<ToolWorld>(trampoline);
        toolWorldInstalled = MH_EnableHook(target) == MH_OK;
        if (!toolWorldInstalled) MH_RemoveHook(target);
        if (!toolWorldInstalled) return false;
        target = reinterpret_cast<void*>(base + controlSetRva);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&setPlayerToolContext), &trampoline) != MH_OK) return false;
        originalControlSet = reinterpret_cast<ControlSet>(trampoline);
        controlSetInstalled = MH_EnableHook(target) == MH_OK;
        if (!controlSetInstalled) MH_RemoveHook(target);
        controlEvents = 0;
        toolFreedomEvents = 0;
        if (!controlSetInstalled) return false;
        target = reinterpret_cast<void*>(base + toolUnlockedRva);
        if (MH_CreateHook(target, reinterpret_cast<void*>(&allowToolUnlocked), &trampoline) != MH_OK) return false;
        originalToolUnlocked = reinterpret_cast<ToolUnlocked>(trampoline);
        refreshTool = reinterpret_cast<ToolRefresh>(base + 0xC1E280);
        toolUnlockedInstalled = MH_EnableHook(target) == MH_OK;
        if (!toolUnlockedInstalled) MH_RemoveHook(target);
        if (!toolUnlockedInstalled || !installCharacterParticles(base, trace)) return false;
        emitterTraceCount = 0;
        target = reinterpret_cast<void*>(base + emitterLookupRva);
        if (!codeMatches(base + emitterLookupRva, "8B 44 24 04 50 81 C1 88 00 00 00 E8") ||
            !callsTo(base, 0x2D8ACB, 0xB75DF0) || !callsTo(base, 0x2EF8DD, emitterLookupRva) ||
            !codeMatches(base + 0x2D8AD7, "8B 40 0C C2 04 00") ||
            MH_CreateHook(target, reinterpret_cast<void*>(&lookupCharacterEmitter), &trampoline) != MH_OK) return false;
        originalEmitterLookup = reinterpret_cast<EmitterLookup>(trampoline);
        emitterInstalled = MH_EnableHook(target) == MH_OK;
        if (!emitterInstalled) MH_RemoveHook(target);
        return emitterInstalled;
    }

    struct Edit {
        std::uint32_t offset;     // From the site.
        const char* original;
        const char* replacement;
    };
    struct Site {
        const char* name;
        std::uint32_t rva;
        const char* pattern;      // Verified at rva before anything is written.
        std::uint32_t findPlaysetCall;  // RVA of a call to FindPlaysetForCharacter in this code, 0 if none.
        Edit edits[4];
    };

    constexpr Site sites[] = {
        {"System_IsCharacterValid", 0x83CC1E,
         "E8 ?? ?? ?? ?? 8B 10 52 8D 4C 24 0C C6 44 24 1C 01 E8 ?? ?? ?? ?? 8D 4C 24 0C E8 ?? ?? ?? ?? "
         "8B 44 24 08 83 78 F4 00 74 38 57 50 E8 ?? ?? ?? ?? 83 C4 08 85 C0 75 2A",
         0x83CC1E, {{0x27, "74 38", "90 90"}, {0x35, "75 2A", "90 90"}}},
        {"figure placement", 0x615EF9, "57 E8 ?? ?? ?? ?? 84 C0 0F 85 CF 00 00 00",
         0x615F84, {{0x06, "84 C0 0F 85 CF 00 00 00", "90 90 E9 D0 00 00 00 90"}}},
        {"character validity for the world", 0x77E0A4,
         "E8 ?? ?? ?? ?? 8B 08 51 8D 4C 24 28 88 5C 24 1C E8 ?? ?? ?? ?? 8D 4C 24 0C E8 ?? ?? ?? ?? "
         "8B 44 24 24 83 78 F4 00 74 3A 8B 54 24 20 52 50 E8 ?? ?? ?? ?? 83 C4 08 85 C0 75 28",
         0x77E0A4, {{0x26, "74 3A", "90 90"}, {0x38, "75 28", "90 90"}}},
        {"zone entry", 0x79A305,
         "E8 ?? ?? ?? ?? 8B 00 50 8D 4C 24 1C C6 44 24 30 05 E8 ?? ?? ?? ?? 8D 4C 24 20 C6 44 24 2C 04 "
         "E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 08 85 C0 75 04 B3 01",
         0x79A305, {{0x34, "75 04", "90 90"}}},
        {"actor lists", 0x28B2BA, "8B 74 24 20 84 C0 75 08 85 F6 74 04 32 DB EB 02 B3 01",
         0, {{0x0C, "32 DB", "B3 01"}}},
        // Loading an actor name does not load its statistics or animation choreography.
        // Use the same complete dependency-map path already used by the Toy Box. The
        // engine sets +0x55 here, so its paired unload also releases every loaded map.
        {"actor dependency maps", 0x28B43A,
         "8B 74 24 20 84 C0 75 08 85 F6 74 04 32 DB EB 02 B3 01",
         0, {{0x0C, "32 DB", "B3 01"}}},
    };

    std::size_t parse(const char* hex, unsigned char (&out)[16]) {
        std::size_t count = 0;
        for (const char* cursor = hex; *cursor && count < sizeof(out);) {
            while (*cursor == ' ') ++cursor;
            if (!*cursor) break;
            char* end{};
            out[count++] = static_cast<unsigned char>(std::strtoul(cursor, &end, 16));
            cursor = end;
        }
        return count;
    }

    bool verified(std::uintptr_t base, const Site& site) {
        if (!codeMatches(base + site.rva, site.pattern)) return false;
        if (site.findPlaysetCall && !callsTo(base, site.findPlaysetCall, findPlaysetRva)) return false;
        for (const auto& edit : site.edits)
            if (edit.original && !codeMatches(base + site.rva + edit.offset, edit.original)) return false;
        return true;
    }

    bool write(std::uintptr_t address, const unsigned char* bytes, std::size_t length) {
        DWORD old{};
        if (!VirtualProtect(reinterpret_cast<void*>(address), length, PAGE_EXECUTE_READWRITE, &old)) return false;
        std::memcpy(reinterpret_cast<void*>(address), bytes, length);
        VirtualProtect(reinterpret_cast<void*>(address), length, old, &old);
        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(address), length);
        return true;
    }
}



bool installCharacterFreedom(HMODULE host, const Config& options) {
    if (!options.charactersInAnyPlayset) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    const char* actorPath = codeMatches(base + 0x28B305, "68 ?? ?? ?? ??") ? *reinterpret_cast<const char* const*>(base + 0x28B306) : nullptr;
    if (!actorPath || !readable(actorPath, 26) || std::memcmp(actorPath, "GameDB/Core/%s_actors.lua", 26)) {
        log("Gameplay: CharactersInAnyPlayset unavailable: actor list loader differs.");
        return false;
    }
    const char* mapPath = codeMatches(base + 0x28B485, "68 ?? ?? ?? ??") ?
        *reinterpret_cast<const char* const*>(base + 0x28B486) : nullptr;
    if (!mapPath || !readable(mapPath, 27) || std::strcmp(mapPath, "GameDB/Core/%sDataMap.lua") ||
        !codeMatches(base + 0x28B564, "C6 45 55 01") ||
        !codeMatches(base + 0x28B6B3, "80 7E 55 00 75 62")) {
        log("Gameplay: CharactersInAnyPlayset unavailable: dependency map loader/unloader differs.");
        return false;
    }
    for (const auto& site : sites)
        if (!verified(base, site)) {
            log("Gameplay: CharactersInAnyPlayset unavailable: %s code differs; nothing changed.", site.name);
            return false;
        }
    if (!codeMatches(base + avatarVariantRva,
        "64 A1 00 00 00 00 6A FF 68 ?? ?? ?? ?? 50 64 89 25 00 00 00 00 8B 0D ?? ?? ?? ?? "
        "8B 81 54 01 00 00 56 6A 00 6A 00 50 68 ?? ?? ?? ?? E8") ||
        !callsTo(base, 0xB01FEC, 0xAFE4B0) || !callsTo(base, 0x616021, avatarVariantRva) ||
        !callsTo(base, 0x61B1AC, avatarVariantRva) || !callsTo(base, 0xB085E0, avatarVariantRva) ||
        !codeMatches(base + 0xAFE4B0,
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 08 "
            "C7 04 24 00 00 00 00 C7 44 24 04 81 7B E1 FF")) {
        log("Gameplay: CharactersInAnyPlayset unavailable: avatar variant code differs; nothing changed.");
        return false;
    }
    zoneManager = *reinterpret_cast<void***>(base + 0xB01FD7);
    if (!readable(zoneManager, sizeof(void*))) return false;
    zoneString = reinterpret_cast<ZoneString>(base + 0xAFE4B0);
    void* trampoline{};
    auto* target = reinterpret_cast<void*>(base + avatarVariantRva);
    if (MH_CreateHook(target, reinterpret_cast<void*>(&chooseAvatarVariant), &trampoline) != MH_OK) {
        log("Gameplay: CharactersInAnyPlayset unavailable: avatar variant hook refused; nothing changed.");
        return false;
    }
    originalVariant = reinterpret_cast<AvatarVariant>(trampoline);
    installedBase = base;
    unsigned count = 0;
    for (const auto& site : sites)
        for (const auto& edit : site.edits) {
            if (!edit.replacement) continue;
            unsigned char bytes[16];
            const auto length = parse(edit.replacement, bytes);
            if (!write(base + site.rva + edit.offset, bytes, length)) {
                // Restore every earlier edit while the variant hook is still disabled.
                removeCharacterFreedom();
                log("Gameplay: CharactersInAnyPlayset: could not write %s; prior edits restored.", site.name);
                return false;
            }
            ++count;
        }
    if (MH_EnableHook(target) != MH_OK) {
        removeCharacterFreedom();
        log("Gameplay: CharactersInAnyPlayset unavailable: avatar hook could not be enabled; edits restored.");
        return false;
    }
    log("Gameplay: characters allowed in every play set (%u native checks relaxed).", count);
    active = true;
    trace = options.traceCharacterFreedom;
    experienceMultiplier = options.testExperienceMultiplier >= 1 && options.testExperienceMultiplier <= 100 ? options.testExperienceMultiplier : 1;
    variantEvents = experienceEvents = 0;
    if (!installToolFreedom(base)) {
        removeCharacterFreedom();
        log("Gameplay: character tool freedom unavailable; native edits restored.");
        return false;
    }
    log("Gameplay: player tool menu, availability refresh and missing particle preload enabled across playsets.");
    if (trace) {
        if (codeMatches(base + 0xAFE260,
            "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 08"))
            zoneBool = reinterpret_cast<ZoneBool>(base + 0xAFE260);
    }
    if (trace || experienceMultiplier != 1) {
        const bool installed = installExperienceTrace(base);
        log("Gameplay: native EXP hook %s; test multiplier=%u; diagnostics=%d.",
            installed ? "enabled" : "unavailable (signature/hook)", experienceMultiplier, trace);
        if (!installed && experienceMultiplier != 1) { removeCharacterFreedom(); return false; }
    }
    if (trace) {
        installToolModelTrace(base);
        installToolControlTrace(base);
    }
    return true;
}

bool characterFreedomActive() noexcept { return active; }

void addCharacterFreedomAdapters(mods::LuaApi& api) {
    if (!active) return;
    lua.toLString = api.toLString;
    lua.pushBoolean = api.pushBoolean;
    api.adapters.push_back({"RegisterPlaysetAvatars", &registerPlaysetAvatars});
    if (api.pushBoolean) api.adapters.push_back({"IsRegisteredAvatar", &isRegisteredAvatar});
}

void removeCharacterFreedom() noexcept {
    if (!installedBase) return;
    removeCharacterParticles();
    for (auto [rva, installed] : {std::pair{toolRuleRva, &toolRuleTraceInstalled}, std::pair{modelRequestRva, &modelTraceInstalled},
        std::pair{toolInitializeRva, &toolInitializeTraceInstalled}, std::pair{controlSetRva, &controlSetInstalled},
        std::pair{toolActionRva, &actionTraceInstalled}, std::pair{toolAssetRva, &toolAssetTraceInstalled},
        std::pair{toolUnlockedRva, &toolUnlockedInstalled}, std::pair{projectileCreateRva, &projectileTraceInstalled},
        std::pair{emitterLookupRva, &emitterInstalled}}) {
        if (!*installed) continue;
        auto* target = reinterpret_cast<void*>(installedBase + rva);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        *installed = false;
    }
    for (auto [rva, installed] : {std::pair{controlGetRva, &menuFreedomInstalled}, std::pair{toolWorldRva, &toolWorldInstalled}}) {
        if (!*installed) continue;
        auto* target = reinterpret_cast<void*>(installedBase + rva);
        MH_DisableHook(target);
        MH_RemoveHook(target);
        *installed = false;
    }
    if (awardTraceInstalled) {
        auto* awardTarget = reinterpret_cast<void*>(installedBase + awardExperienceRva);
        MH_DisableHook(awardTarget);
        MH_RemoveHook(awardTarget);
        awardTraceInstalled = false;
    }
    trace = false;
    controlGet = nullptr;
    actorData = nullptr;
    stateDataType = nullptr;
    experienceMultiplier = 1;
    auto* target = reinterpret_cast<void*>(installedBase + avatarVariantRva);
    MH_DisableHook(target);
    MH_RemoveHook(target);
    for (const auto& site : sites)
        for (const auto& edit : site.edits) {
            if (!edit.original) continue;
            // An untouched edit already contains the original bytes.
            if (!codeMatches(installedBase + site.rva + edit.offset, edit.replacement)) continue;
            unsigned char bytes[16];
            const auto length = parse(edit.original, bytes);
            if (!write(installedBase + site.rva + edit.offset, bytes, length))
                log("Gameplay: rollback failed for %s.", site.name);
        }
    installedBase = 0;
    active = false;
}
}
