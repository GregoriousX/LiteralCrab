// SPDX-License-Identifier: GPL-3.0-only
// DI1 profile for the script mod loader: the measured Lua 5.1 C API of the supported build.
// Another game needs only its own table of addresses and signatures; src/mods is shared.
#include "di1/mods.hpp"
#include "di1/adapters.hpp"
#include "di1/asset_loader.hpp"
#include "di1/audio_banks.hpp"
#include "di1/character_freedom.hpp"
#include "di1/character_targets.hpp"
#include "di1/common.hpp"
#include "di1/gui_screens.hpp"
#include "di1/toybox_share.hpp"
#include "minhook/MinHook.h"
#include "mods/file_redirect.hpp"
#include "mods/lua_mods.hpp"
#include <cwchar>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace crabe::di1 {
namespace {
    struct Function {
        std::uint32_t rva;
        const char* prologue;
        std::uint32_t callSite;  // A stdlib call to this function confirms its identity.
    };
    constexpr std::uint32_t loadBufferRva = 0x65C40;
    constexpr Function luaFunctions[] = {
        {loadBufferRva, "83 EC 08 8B 44 24 10 8B 54 24 18 8B 4C 24 14 52", 0x73239},
        {0x63EA0, "8B 4C 24 04 8B 41 08 2B 41 0C C1 F8 03 C3", 0x72A79},              // lua_gettop
        {0x63EB0, "8B 4C 24 08 8B 44 24 04 85 C9 78 37 8B 50 0C", 0x72AF9},           // lua_settop
        {0x64060, "8B 4C 24 08 56 8B 74 24 08 8B D6 E8 00 FC FF FF", 0x72AA3},       // lua_pushvalue
        {0x63F90, "56 8B 74 24 08 57 8B 7C 24 10 81 FF EF D8 FF FF", 0x7359A},       // lua_insert
        {0x646A0, "56 57 8B 7C 24 0C 8B 47 10 8B 48 44 3B 48 40", 0x73A93},          // lua_pushcclosure
        {0x64D20, "8B 4C 24 10 83 EC 08 56 8B 74 24 10 85 C9 75 04", 0x73584},       // lua_pcall
        {0x64360, "56 8B 74 24 08 57 8B 7C 24 10 8B CF 8B D6", 0x72ABE},             // lua_tolstring
    };
    // Used by adapters only; confirmed through their calls in luaB_tonumber, assert, rawequal, type.
    constexpr Function adapterLuaFunctions[] = {
        {0x642B0, "8B 4C 24 08 8B 54 24 04 83 EC 08 E8 B0 F9 FF FF", 0x72B7B},       // lua_tonumber
        {0x64330, "8B 4C 24 08 8B 54 24 04 E8 33 F9 FF FF 8B 48 04", 0x733B0},       // lua_toboolean
        {0x64720, "8B 44 24 04 8B 48 08 33 D2 39 54 24 08 C7 41 04", 0x72F51},       // lua_pushboolean
        {0x64090, "8B 4C 24 08 8B 54 24 04 E8 D3 FB FF FF 3D", 0x730C0},             // lua_type
        {0x64580, "8B 4C 24 04 8B 41 08 F2 0F 10 44 24 08 F2 0F 11", 0x72B84},       // lua_pushnumber
    };

    // luaL_Reg arrays of the statically linked standard library: {name, function} pairs ending in
    // {NULL, NULL}. Patches get allowlisted entries even in states opened without the library.
    struct Library {
        std::uint32_t rva;
        const char* prefix;
        std::size_t count;
    };
    constexpr Library libraries[] = {
        {0xE99778, "", 26},         // base (includes the engine's own dumpobject entries)
        {0xE99158, "table.", 9},
        {0xE98A50, "string.", 15},
        {0xE988A0, "math.", 28},
    };
    // [ToyBox] LimitScale: GameDB/Core/ToyBoxStats_Limits.lua defines the Toy Box editor's
    // usage limits (object costs, measured memory, frame time) that fill its meter and stop
    // further placement. Every number in UsageLimits and NetworkLimits is multiplied.
    constexpr std::string_view toyBoxLimitsPatch = R"lua(
local _, options, log = ...
local limits = ToyBoxLimits
if type(limits) ~= "table" then
    log("ToyBoxLimits not found; the game's limits are unchanged")
    return
end
local changed = 0
for _, group in pairs({limits.UsageLimits, limits.NetworkLimits}) do
    if type(group) == "table" then
        for key, value in pairs(group) do
            if type(value) == "number" then
                group[key] = value * options.Scale
                changed = changed + 1
            end
        end
    end
end
log("APPLIED: " .. changed .. " Toy Box limits multiplied by " .. options.Scale)
)lua";

    constexpr std::uint32_t codeStart = 0x1000, codeEnd = 0x1000 + 0xE52E4A;
    constexpr std::uint32_t imageSize = 0x1511000;

    bool readLibrary(std::uintptr_t base, const Library& library, std::vector<mods::LibraryFunction>& out) {
        struct Entry { const char* name; mods::LuaCFunction function; };
        const auto* entries = reinterpret_cast<const Entry*>(base + library.rva);
        if (!readable(entries, sizeof(Entry) * (library.count + 1)) ||
            entries[library.count].name || entries[library.count].function) return false;
        for (std::size_t index = 0; index < library.count; ++index) {
            const auto name = reinterpret_cast<std::uintptr_t>(entries[index].name);
            const auto function = reinterpret_cast<std::uintptr_t>(entries[index].function);
            if (name < base || name >= base + imageSize - 32 || !readable(entries[index].name, 32) ||
                function < base + codeStart || function >= base + codeEnd) return false;
            std::size_t length = 0;
            while (length < 32 && entries[index].name[length] > ' ' && entries[index].name[length] < 127) ++length;
            if (!length || length == 32 || entries[index].name[length]) return false;
            out.push_back({std::string(library.prefix) + std::string(entries[index].name, length), entries[index].function});
        }
        return true;
    }


    template <class T> void bind(T& slot, std::uintptr_t address) { slot = reinterpret_cast<T>(address); }

    void installAssets(HMODULE host, const std::wstring& modsRoot, const std::vector<mods::Mod>& list,
        std::vector<std::string>& messages) {
        const bool trace = config().modTrace;
        wchar_t executable[32768]{};
        const auto length = GetModuleFileNameW(host, executable, 32768);
        const wchar_t* slash = length && length < 32768 ? wcsrchr(executable, L'\\') : nullptr;
        if (list.empty() || !slash) {
            if (trace || config().traceCharacterFreedom) installAssetLoader(host, nullptr, trace);
            return;
        }
        const auto gameAssets = std::wstring(executable, static_cast<std::size_t>(slash - executable)) + L"\\assets";
        const auto started = GetTickCount64();
        auto assets = mods::AssetIndex::build(gameAssets, modsRoot + L"\\.cache", list, messages);
        const auto redirects = assets.redirects.size(), members = assets.members.size();
        bool audioBanks{};
        for (const auto& asset : assets.redirects)
            if (asset.path.starts_with("sound/") && asset.path.ends_with(".fsb")) { audioBanks = true; break; }
        // The index is kept first: the loader hook asks it for files as soon as it is enabled.
        if (!mods::installFileRedirect(std::move(assets), &log, trace, config().traceAudioBanks) && redirects)
            messages.push_back("Mods: files the game opens from disk cannot be replaced; those stay original.");
        if ((members || trace || config().traceCharacterFreedom) && !installAssetLoader(host, members ? &mods::readAssetMember : nullptr, trace) && members)
            messages.push_back("Mods: files inside the game's archives cannot be replaced on this build.");
        if (audioBanks && config().traceAudioBanks && !installAudioBankTrace(host))
            messages.push_back("Mods: native audio-bank diagnostics unavailable on this profile.");
        if (redirects || members)
            messages.push_back("Mods: " + std::to_string(redirects) + " file(s) replaced on disk, " + std::to_string(members) +
              " served to the engine's loader; prepared before game/audio startup in " + std::to_string(GetTickCount64() - started) + " ms.");
    }

    // [Engine] features: each is installed once, and only when an enabled mod requests it.
    void installEngineFeatures(HMODULE host, const std::vector<mods::Mod>& list) {
        bool guiScreens = false;
        for (const auto& mod : list)
            for (const auto& feature : mod.engine) {
                if (feature == "TriggerGuiScreen") guiScreens = true;
                else log("Mod %s: unknown engine feature %s ignored.", mod.folder.c_str(), feature.c_str());
            }
        if (guiScreens) installGuiScreenCommands(host);
    }
}

bool installMods(HMODULE host) {
    const auto& settings = config();
    if (!settings.mods) {
        log("Mods: disabled by configuration.%s%s", settings.toyBoxLimitScale > 1 ? " ToyBox LimitScale needs [Mods] Enabled=1." : "",
            toyBoxShareActive() ? " The Toy Box Share entry needs [Mods] Enabled=1." : "");
        return true;
    }
    if (!*pluginDirectory()) return false;
    try {
        std::vector<std::string> messages;
        const auto modsRoot = std::wstring(pluginDirectory()) + L"mods";
        auto index = mods::Index::load(modsRoot, messages);
        // Assets do not depend on the Lua profile: they only redirect files the game opens.
        installAssets(host, modsRoot, index.mods, messages);
        for (const auto& message : messages) log("%s", message.c_str());
        installEngineFeatures(host, index.mods);
        if (characterFreedomActive()) {
            index.addBuiltinPatch({"bitmasks.lua", "contains:PC_RespawnDeath"}, "character_reset_filter",
                std::string(characterResetFilterPatch), "{}");
            index.addBuiltinPatch({"zonelist.lua", "contains:PlayableAvatars"}, "character_playsets", std::string(characterPlaysetsPatch), "{}");
            index.addBuiltinPatch({"contains:ActorList"}, "character_actor_registry", std::string(characterToyboxActorsPatch), "{}");
            for (const auto [file, marker, home] : {
                std::tuple{"incredibles_actors.lua", "contains:av_mrincredible_ps", "Incredibles"},
                std::tuple{"monstersu_actors.lua", "contains:mu_randall_ps", "MonstersU"},
                std::tuple{"pirates_actors.lua", "contains:pir_jacksparrow_ps", "Pirates"},
                std::tuple{"loneranger_actors.lua", "contains:lr_loneranger_ps", "LoneRanger"},
                std::tuple{"toystoryinspace_actors.lua", "contains:av_buzz_ps", "ToyStoryInSpace"}})
                index.addBuiltinPatch({file, marker}, std::string("character_native_home_") + home,
                    std::string(characterNativeActorHomesPatch), std::string("{Playset='") + home + "'}");
            index.addBuiltinPatch({"contains:AvatarInteractionHud", "contains:SummonMount"},
                "character_mounts", std::string(characterMountsPatch), "{}");
            if (installCharacterTargets(reinterpret_cast<std::uintptr_t>(host))) {
                index.addBuiltinPatch({"lr_shoottargets.dnax", "contains:target_bullseye"},
                    "character_targets", std::string(characterTargetsPatch), "{}");
                log("Character freedom: bounded ordinary-target projectile gate enabled.");
            } else log("Character freedom: ordinary-target signature/hook differs; target definitions unchanged.");
            index.addBuiltinPatch({"coredatamap.lua", "rumpusroomdatamap.lua", "contains:_loadforallplaysets_", "contains:avatarstats"},
                "character_preloads", std::string(characterPreloadsPatch), "{}");
            if (settings.traceCharacterFreedom) {
                index.addBuiltinPatch({"contains:Customization"}, "character_customization_trace",
                    std::string(characterCustomizationTracePatch), "{}");
                index.addBuiltinPatch({"contains:avatarstats"}, "character_dependency_trace", std::string(characterDependenciesTracePatch), "{}");
                index.addBuiltinPatch({"contains:SPARKS_COLLECTED"}, "character_stats_trace", std::string(characterStatsTracePatch), "{}");
            }
        }
        if (toyBoxShareActive())
            index.addBuiltinPatch({"presentation/toyboxselectmenu.lua"}, "toybox_share", std::string(toyBoxShareMenuPatch), "{}");
        if (settings.toyBoxLimitScale > 1)
            index.addBuiltinPatch({"toyboxstats_limits.lua", "contains:ToyBoxLimits ="}, "toybox_limits", std::string(toyBoxLimitsPatch),
                "{Scale=" + std::to_string(settings.toyBoxLimitScale) + "}");
        if (index.empty()) {
            log("Mods: no script mods to load; game scripts untouched.");
            return true;
        }
        const auto base = reinterpret_cast<std::uintptr_t>(host);
        for (const auto& function : luaFunctions)
            if (!codeMatches(base + function.rva, function.prologue) || !callsTo(base, function.callSite, function.rva)) {
                log("Mods: Lua API signature differs at RVA 0x%X; mod loader not installed.", function.rva);
                return false;
            }
        mods::LuaApi api{};
        bind(api.getTop, base + luaFunctions[1].rva);
        bind(api.setTop, base + luaFunctions[2].rva);
        bind(api.pushValue, base + luaFunctions[3].rva);
        bind(api.insert, base + luaFunctions[4].rva);
        bind(api.pushClosure, base + luaFunctions[5].rva);
        bind(api.pcall, base + luaFunctions[6].rva);
        bind(api.toLString, base + luaFunctions[7].rva);
        bool adapterLua = true;
        for (const auto& function : adapterLuaFunctions)
            adapterLua = adapterLua && codeMatches(base + function.rva, function.prologue) && callsTo(base, function.callSite, function.rva);
        if (adapterLua) {
            bind(api.toNumber, base + adapterLuaFunctions[0].rva);
            bind(api.toBoolean, base + adapterLuaFunctions[1].rva);
            bind(api.pushBoolean, base + adapterLuaFunctions[2].rva);
            bind(api.type, base + adapterLuaFunctions[3].rva);
            bind(api.pushNumber, base + adapterLuaFunctions[4].rva);
            api.adapters = resolveAdapters(host, api, characterToolQueryHook());
        } else {
            log("Mods: Lua value functions differ; no LiteralCrab adapters.");
        }
        addCharacterFreedomAdapters(api);
        for (const auto& library : libraries)
            if (!readLibrary(base, library, api.library)) {
                log("Mods: Lua %slibrary table differs at RVA 0x%X; mod loader not installed.", library.prefix, library.rva);
                return false;
            }
        auto* target = reinterpret_cast<void*>(base + loadBufferRva);
        void* original{};
        if (const auto status = MH_CreateHook(target, reinterpret_cast<void*>(&mods::loadBuffer), &original); status != MH_OK) {
            log("Mods: hook refused: %s.", MH_StatusToString(status));
            return false;
        }
        bind(api.loadBuffer, reinterpret_cast<std::uintptr_t>(original));
        const auto modCount = index.mods.size(), targets = index.targets.size() + index.contents.size();
        if (!mods::configure(api, std::move(index), &log, settings.modTrace)) {
            MH_RemoveHook(target);
            return false;
        }
        if (const auto status = MH_EnableHook(target); status != MH_OK) {
            MH_RemoveHook(target);
            log("Mods: hook refused: %s.", MH_StatusToString(status));
            return false;
        }
        log("Mods: %zu mod(s) loaded, %zu game chunk(s) targeted.%s", modCount, targets,
            settings.modTrace ? " Trace logs every compiled Lua chunk." : "");
        return true;
    } catch (const std::exception& failure) {
        log("Mods: loader unavailable: %s.", failure.what());
        return false;
    }
}
}
