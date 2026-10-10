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

    bool toolsResolved() {
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
            !codeMatches(base + unlockedRva, "8B 44 24 04 53 55 56 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8") || !callsTo(base, 0x5FFBEB, unlockedRva) ||
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
        if (number < 0 || number > 7 || number != static_cast<int>(number)) return false;
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

    // LiteralCrab.HasToolSet(player) -> the player currently has an avatar with tools.
    int __cdecl hasToolSet(lua_State* state) {
        if (lua.type(state, 1) != 3) return result(state, false);
        const double number = lua.toNumber(state, 1);
        return result(state, number >= 0 && number <= 7 && number == static_cast<int>(number) && toolSet(static_cast<int>(number)));
    }
}

std::vector<mods::LibraryFunction> resolveAdapters(HMODULE host, const mods::LuaApi& api) {
    std::vector<mods::LibraryFunction> adapters;
    if (!api.toNumber || !api.toBoolean || !api.pushBoolean || !api.type || !api.toLString) return adapters;
    lua = api;
    base = reinterpret_cast<std::uintptr_t>(host);
    if (toolsResolved()) {
        adapters.push_back({"AwardTool", &awardTool});
        adapters.push_back({"UnAwardTool", &unawardTool});
        adapters.push_back({"IsToolAwarded", &isToolAwarded});
        adapters.push_back({"IsToolUnlocked", &isToolUnlocked});
        adapters.push_back({"HasToolSet", &hasToolSet});
        log("Mods: adapters available to patches: AwardTool, UnAwardTool, IsToolAwarded, IsToolUnlocked, HasToolSet.");
    } else {
        log("Mods: tool adapter signatures differ; LiteralCrab.AwardTool is unavailable.");
    }
    return adapters;
}
}
