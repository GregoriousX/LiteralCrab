// SPDX-License-Identifier: GPL-3.0-only
// DI1 adapters: small engine capabilities that the game's Lua states do not expose, reached
// only through functions measured in the supported build.
//
// Tools: a player's avatar owns a ToolSetManager component. AwardToolByName is what the game's
// own AwardTool/ForceAwardTool actor messages run; it applies the native playset and avatar
// rules and sets the tool's awarded flag.
#include "di1/adapters.hpp"
#include "di1/common.hpp"
#include <cstdint>
#include <cstring>
#include <cmath>

namespace crabe::di1 {
namespace {
    using lua_State = ::lua_State;
    mods::LuaApi lua{};
    std::uintptr_t base{};

    // Player manager global, read from its use in Players_GetAvatarFromPlayerId.
    constexpr std::uint32_t avatarNativeRva = 0xADD360, playerManagerLoad = 0xADD36E;
    constexpr std::uint32_t playerCountRva = 0xAD85E0, actorFromHandleRva = 0x326030, componentRva = 0x31F6B0;
    constexpr std::uint32_t awardRva = 0xC1F130, unawardRva = 0xC1F1D0, unlockedRva = 0xC1A600, awardedRva = 0xC1A660;
    // ToolSetMgr_ChangeInventoryCount pushes the "ToolSetManager" component name here.
    constexpr std::uint32_t componentNamePush = 0x5FFAA4;
    constexpr std::size_t maxToolName = 64;

    using PlayerCountFn = int(__thiscall*)(void*);
    using ActorFromHandleFn = void*(__cdecl*)(std::uint32_t);
    using ComponentFn = void*(__thiscall*)(void*, const char*);
    using AwardFn = void(__thiscall*)(void*, const char*, int, int);
    using UnawardFn = void(__thiscall*)(void*, const char*);
    using QueryFn = bool(__thiscall*)(void*, const char*);
    void** playerManager{};
    const char* componentName{};
    PlayerCountFn playerCount{};
    ActorFromHandleFn actorFromHandle{};
    ComponentFn component{};
    AwardFn award{};
    UnawardFn unaward{};
    QueryFn isUnlocked{}, isAwarded{};

    bool jumpTo(std::uintptr_t from, const void* to) noexcept {
        __try {
            return *reinterpret_cast<const unsigned char*>(from) == 0xE9 &&
                from + 5 + *reinterpret_cast<const std::int32_t*>(from + 1) == reinterpret_cast<std::uintptr_t>(to);
        } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
    }

    bool toolQueryResolved(ToolQueryHook hook) {
        constexpr auto prefix = "8B 44 24 04 53"; // Two complete native instructions, five bytes.
        const auto entry = base + unlockedRva;
        if (codeMatches(entry, prefix)) return true;
        // CharactersInAnyPlayset installs this query hook before script adapters
        // are resolved. Accept only its exact owned detour and MinHook trampoline,
        // including the original five bytes and jump back to the unchanged body.
        const auto trampoline = reinterpret_cast<std::uintptr_t>(hook.trampoline);
        return hook.detour && trampoline && jumpTo(entry, hook.detour) &&
            codeMatches(trampoline, prefix) && jumpTo(trampoline + 5, reinterpret_cast<const void*>(entry + 5));
    }

    bool toolsResolved(ToolQueryHook hook) {
        if (!codeMatches(base + avatarNativeRva, "53 8B 5C 24 08 56 57 8B CB E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B F0 8B F9 85 F6 78 22 E8") ||
            !codeMatches(base + avatarNativeRva + 0x25, "8B 87 E8 00 00 00 C1 E6 06 8B 74 30 38") ||
            !callsTo(base, 0xADD37C, playerCountRva) || !codeMatches(base + playerCountRva, "E9") ||
            !codeMatches(base + actorFromHandleRva, "83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D") || !callsTo(base, 0x30D89C, actorFromHandleRva) ||
            !codeMatches(base + componentRva, "8B 89 F0 00 00 00 85 C9 74 05 E9") || !callsTo(base, 0x5FFAAB, componentRva) ||
            !codeMatches(base + componentNamePush, "68 ?? ?? ?? ?? 8B C8 E8") ||
            !codeMatches(base + awardRva, "51 8B 44 24 08 53 57 8B D9 8B 0D ?? ?? ?? ?? 50 E8") ||
            !codeMatches(base + 0xC1F6F0, "8B 44 24 04 8B 49 20 6A 00 6A 00 50 E8") || !callsTo(base, 0xC1F6FC, awardRva) ||
            !codeMatches(base + 0xC1F6D0, "8B 44 24 04 8B 49 20 6A 00 6A 01 50 E8") || !callsTo(base, 0xC1F6DC, awardRva) ||
            !codeMatches(base + unawardRva, "51 8B 44 24 08 55 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8") || !callsTo(base, 0xC1F718, unawardRva) ||
            !toolQueryResolved(hook) ||
            !codeMatches(base + unlockedRva + 5, "55 56 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8") || !callsTo(base, 0x5FFBEB, unlockedRva) ||
            !codeMatches(base + awardedRva, "8B 44 24 04 53 55 56 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8") || !callsTo(base, 0xC1A6AC, 0xC19E10))
            return false;
        // Absolute operands are relocated with the game, so they are read, not assumed.
        playerManager = *reinterpret_cast<void***>(base + playerManagerLoad + 2);
        componentName = *reinterpret_cast<const char**>(base + componentNamePush + 1);
        if (!readable(playerManager, sizeof(void*)) || !readable(componentName, 15) || std::strcmp(componentName, "ToolSetManager"))
            return false;
        playerCount = reinterpret_cast<PlayerCountFn>(base + playerCountRva);
        actorFromHandle = reinterpret_cast<ActorFromHandleFn>(base + actorFromHandleRva);
        component = reinterpret_cast<ComponentFn>(base + componentRva);
        award = reinterpret_cast<AwardFn>(base + awardRva);
        unaward = reinterpret_cast<UnawardFn>(base + unawardRva);
        isUnlocked = reinterpret_cast<QueryFn>(base + unlockedRva);
        isAwarded = reinterpret_cast<QueryFn>(base + awardedRva);
        return true;
    }

    // The player's ToolSetManager, or null when there is no such player or avatar right now.
    void* toolSet(int player) noexcept {
        __try {
            void* manager = *playerManager;
            if (!manager || player < 0 || player >= playerCount(manager)) return nullptr;
            const auto* players = *reinterpret_cast<const unsigned char* const*>(static_cast<const unsigned char*>(manager) + 0xE8);
            const auto handle = *reinterpret_cast<const std::uint32_t*>(players + player * 64 + 0x38);
            void* actor = handle ? actorFromHandle(handle) : nullptr;
            return actor ? component(actor, componentName) : nullptr;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return nullptr;
        }
    }

    // Arguments: player index (number) and tool name (string). Copies the name so the call does
    // not depend on Lua keeping the string alive.
    bool arguments(lua_State* state, int& player, char (&name)[maxToolName]) noexcept {
        if (lua.type(state, 1) != 3 || lua.type(state, 2) != 4) return false;  // LUA_TNUMBER, LUA_TSTRING
        const double number = lua.toNumber(state, 1);
        if (!std::isfinite(number) || number < 0 || number > 7 || number != static_cast<int>(number)) return false;
        std::size_t length{};
        const char* text = lua.toLString(state, 2, &length);
        if (!text || !length || length >= maxToolName) return false;
        for (std::size_t index = 0; index < length; ++index)
            if (text[index] <= ' ' || text[index] >= 127) return false;
        std::memcpy(name, text, length);
        name[length] = '\0';
        player = static_cast<int>(number);
        return true;
    }

    int result(lua_State* state, bool value) {
        lua.pushBoolean(state, value);
        return 1;
    }

    bool query(QueryFn function, void* tools, const char* name) noexcept {
        __try {
            return function(tools, name);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // LiteralCrab.AwardTool(player, tool[, force]) -> awarded. Already awarded tools are left
    // alone, so repeated calls (e.g. on every menu open) change nothing.
    int __cdecl awardTool(lua_State* state) {
        int player{};
        char name[maxToolName];
        if (!arguments(state, player, name)) return result(state, false);
        const bool force = lua.toBoolean(state, 3) != 0;
        void* tools = toolSet(player);
        if (!tools) return result(state, false);
        if (query(isAwarded, tools, name)) return result(state, true);
        bool awarded = false;
        __try {
            award(tools, name, force ? 1 : 0, 0);
            awarded = isAwarded(tools, name);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("Adapter AwardTool: native fault 0x%08lX for %s.", GetExceptionCode(), name);
        }
        return result(state, awarded);
    }

    // LiteralCrab.UnAwardTool(player, tool) -> true when the tool is no longer awarded.
    int __cdecl unawardTool(lua_State* state) {
        int player{};
        char name[maxToolName];
        if (!arguments(state, player, name)) return result(state, false);
        void* tools = toolSet(player);
        if (!tools) return result(state, false);
        bool removed = false;
        __try {
            if (isAwarded(tools, name)) unaward(tools, name);
            removed = !isAwarded(tools, name);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("Adapter UnAwardTool: native fault 0x%08lX for %s.", GetExceptionCode(), name);
        }
        return result(state, removed);
    }

    int __cdecl isToolAwarded(lua_State* state) {
        int player{};
        char name[maxToolName];
        void* tools = arguments(state, player, name) ? toolSet(player) : nullptr;
        return result(state, tools && query(isAwarded, tools, name));
    }

    int __cdecl isToolUnlocked(lua_State* state) {
        int player{};
        char name[maxToolName];
        void* tools = arguments(state, player, name) ? toolSet(player) : nullptr;
        return result(state, tools && query(isUnlocked, tools, name));
    }

    // Round power discs: the PC virtual reader feeds the portal emulation with tags
    // (VirtualGateway::AddTag/RemoveTag), which raise the same GatewayTagChange messages a real
    // base does; figures use antenna 0 and hex discs antenna 2. A round disc sits on its
    // figure's antenna. Only IDs the game's tag database classes as round discs are accepted,
    // so figures, play set pieces and hex discs stay with the game's own reader.
    constexpr std::uint32_t addTagRva = 0x877B0, removeTagRva = 0x827E0, findTagRva = 0x7E620, gatewayLoad = 0x833E75;
    // Tag database categories, measured in game: -1 unknown, 0 figures (1000001-1000038),
    // 1 round discs (3000003-3000017, 3000062-3000063), 2 and 3 hex discs (4000018-4000070),
    // 4 play set and Toy Box pieces (2000001-2000007).
    constexpr int roundDiscCategory = 1;
    using AddTagFn = void(__thiscall*)(void*, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
        std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t);
    using RemoveTagFn = void(__thiscall*)(void*, std::uint32_t, std::uint32_t);
    using FindTagFn = void*(__thiscall*)(void*, std::uint32_t, std::uint32_t);
    // The tag database lookup AddTag uses: a tag ID's category, -1 if unknown.
    constexpr std::uint32_t tagCategoryRva = 0x85A80;
    using TagCategoryFn = int(__thiscall*)(void*, std::uint32_t);
    TagCategoryFn tagCategory{};
    void** gateway{};
    AddTagFn addTag{};
    RemoveTagFn removeTag{};
    FindTagFn findTag{};

    bool discsResolved() {
        if (!codeMatches(base + addTagRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 3C 56 8B F1") ||
            !codeMatches(base + removeTagRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 34 53 55 8B 6C 24 4C") ||
            !codeMatches(base + findTagRva, "53 56 8B 74 24 0C 57 8B 7C 24 14 33 D2") ||
            !codeMatches(base + gatewayLoad, "8B 0D ?? ?? ?? ?? E8") ||
            !codeMatches(base + tagCategoryRva, "55 8B EC 83 E4 F8 6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 48 53 8B D9") ||
            !callsTo(base, 0x833EBF, addTagRva) || !callsTo(base, 0x833E7B, removeTagRva) || !callsTo(base, 0x82806, findTagRva) ||
            !callsTo(base, 0x87832, tagCategoryRva))
            return false;
        gateway = *reinterpret_cast<void***>(base + gatewayLoad + 2);
        if (!readable(gateway, sizeof(void*))) return false;
        addTag = reinterpret_cast<AddTagFn>(base + addTagRva);
        removeTag = reinterpret_cast<RemoveTagFn>(base + removeTagRva);
        findTag = reinterpret_cast<FindTagFn>(base + findTagRva);
        tagCategory = reinterpret_cast<TagCategoryFn>(base + tagCategoryRva);
        return true;
    }

    int categoryOf(std::uint32_t id) noexcept {
        __try {
            void* reader = *gateway;
            return reader ? tagCategory(reader, id) : -1;
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return -1;
        }
    }

    bool idArgument(lua_State* state, int index, std::uint32_t& id) noexcept {
        if (lua.type(state, index) != 3) return false;
        const double number = lua.toNumber(state, index);
        if (!std::isfinite(number) || number < 1 || number > 99999999 || number != static_cast<std::uint32_t>(number)) return false;
        id = static_cast<std::uint32_t>(number);
        return true;
    }

    bool discArgument(lua_State* state, int index, std::uint32_t& disc) noexcept {
        return idArgument(state, index, disc) && categoryOf(disc) == roundDiscCategory;
    }

    bool discPlaced(std::uint32_t disc) noexcept {
        __try {
            void* reader = *gateway;
            return reader && findTag(reader, disc, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }

    // LiteralCrab.PlaceDisc(player, disc) -> placed. Puts a round disc on the player's figure
    // antenna, as if it were set under the figure; placing it again changes nothing.
    int __cdecl placeDisc(lua_State* state) {
        std::uint32_t disc{};
        if (lua.type(state, 1) != 3 || !discArgument(state, 2, disc)) return result(state, false);
        const double player = lua.toNumber(state, 1);
        if (player != 0 && player != 1) return result(state, false);
        if (discPlaced(disc)) return result(state, true);
        __try {
            void* reader = *gateway;
            if (reader) addTag(reader, disc, 0, disc, static_cast<std::uint32_t>(player), 0, 0, 0, 0, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("Adapter PlaceDisc: native fault 0x%08lX for %u.", GetExceptionCode(), disc);
        }
        return result(state, discPlaced(disc));
    }

    // LiteralCrab.RemoveDisc(disc) -> true when the disc is no longer placed.
    int __cdecl removeDisc(lua_State* state) {
        std::uint32_t disc{};
        if (!discArgument(state, 1, disc)) return result(state, false);
        if (!discPlaced(disc)) return result(state, true);
        __try {
            void* reader = *gateway;
            if (reader) removeTag(reader, disc, 0);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            log("Adapter RemoveDisc: native fault 0x%08lX for %u.", GetExceptionCode(), disc);
        }
        return result(state, !discPlaced(disc));
    }

    // LiteralCrab.TagCategory(id) -> the game's tag category (see above), -1 when unknown.
    int __cdecl tagCategoryOf(lua_State* state) {
        std::uint32_t id{};
        lua.pushNumber(state, idArgument(state, 1, id) ? categoryOf(id) : -1);
        return 1;
    }

    int __cdecl isDiscPlaced(lua_State* state) {
        std::uint32_t disc{};
        return result(state, discArgument(state, 1, disc) && discPlaced(disc));
    }

    // AddTag's verified initializer stores its antenna argument at record+0x14.
    // This extra layout check is independent of the existing disc adapters.
    bool discOwnerResolved() {
        return codeMatches(base + 0x7E370,
            "8B 44 24 04 80 89 5C 01 00 00 02 8B 54 24 08 89 41 08 8B 44 24 10 89 41 14 89 51 0C") &&
            codeMatches(base + 0x7E630,
            "52 E8 CA FF FF FF 39 70 08 75 05 39 78 0C 74 08 42 83 FA 0C 72 EA 33 C0 5F 5E 5B C2 08 00") &&
            callsTo(base, 0x87826, 0x7E370);
    }

    // Actual placed tag ownership, independent of Lua/menu lifetime and delayed
    // avatar effects. 0/1: player; -1: valid round disc absent; -2: unknown/error.
    int __cdecl getDiscOwner(lua_State* state) {
        int owner = -2;
        std::uint32_t disc{};
        if (discArgument(state, 1, disc)) {
            __try {
                void* reader = *gateway;
                if (reader) {
                    const auto* tag = static_cast<const std::uint8_t*>(findTag(reader, disc, 0));
                    if (!tag) owner = -1;
                    else if (readable(tag, 0x18)) {
                        std::uint32_t low{}, high{}, antenna{};
                        std::memcpy(&low, tag + 8, sizeof(low));
                        std::memcpy(&high, tag + 0xC, sizeof(high));
                        std::memcpy(&antenna, tag + 0x14, sizeof(antenna));
                        if (low == disc && high == 0 && antenna <= 1) owner = static_cast<int>(antenna);
                    }
                }
            } __except (EXCEPTION_EXECUTE_HANDLER) {
                log("Adapter GetDiscOwner: native fault 0x%08lX for %u.", GetExceptionCode(), disc);
            }
        }
        lua.pushNumber(state, owner);
        return 1;
    }

    // LiteralCrab.HasToolSet(player) -> the player currently has an avatar with tools.
    int __cdecl hasToolSet(lua_State* state) {
        if (lua.type(state, 1) != 3) return result(state, false);
        const double number = lua.toNumber(state, 1);
        return result(state, std::isfinite(number) && number >= 0 && number <= 7 && number == static_cast<int>(number) && toolSet(static_cast<int>(number)));
    }
}

std::vector<mods::LibraryFunction> resolveAdapters(HMODULE host, const mods::LuaApi& api, ToolQueryHook toolQuery) {
    std::vector<mods::LibraryFunction> adapters;
    if (!api.toNumber || !api.toBoolean || !api.pushBoolean || !api.type || !api.toLString) return adapters;
    lua = api;
    base = reinterpret_cast<std::uintptr_t>(host);
    if (toolsResolved(toolQuery)) {
        adapters.push_back({"AwardTool", &awardTool});
        adapters.push_back({"UnAwardTool", &unawardTool});
        adapters.push_back({"IsToolAwarded", &isToolAwarded});
        adapters.push_back({"IsToolUnlocked", &isToolUnlocked});
        adapters.push_back({"HasToolSet", &hasToolSet});
        log("Mods: adapters available to patches: AwardTool, UnAwardTool, IsToolAwarded, IsToolUnlocked, HasToolSet.");
    } else {
        log("Mods: tool adapter signatures differ; LiteralCrab.AwardTool is unavailable.");
    }
    if (discsResolved()) {
        adapters.push_back({"PlaceDisc", &placeDisc});
        adapters.push_back({"RemoveDisc", &removeDisc});
        adapters.push_back({"IsDiscPlaced", &isDiscPlaced});
        if (api.pushNumber) adapters.push_back({"TagCategory", &tagCategoryOf});
        log("Mods: adapters available to patches: PlaceDisc, RemoveDisc, IsDiscPlaced, TagCategory.");
        if (api.pushNumber && discOwnerResolved()) {
            adapters.push_back({"GetDiscOwner", &getDiscOwner});
            log("Mods: read-only disc ownership adapter available: GetDiscOwner.");
        } else {
            log("Mods: tag owner layout differs or numeric results unavailable; GetDiscOwner unavailable, existing disc adapters retained.");
        }
    } else {
        log("Mods: virtual reader signatures differ; LiteralCrab.PlaceDisc is unavailable.");
    }
    return adapters;
}
}
