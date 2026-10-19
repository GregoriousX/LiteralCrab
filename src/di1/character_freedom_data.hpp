// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace crabe::di1 {
inline constexpr std::string_view characterTargetsPatch = R"lua(
local _, options, log = ...
local gadgets = type(DNA) == "table" and DNA.AgentGadgets
if type(gadgets) ~= "table" then return end
local exploder, effects = gadgets.Exploder, gadgets.Effects
if type(exploder) ~= "table" or type(exploder.WeaponList) ~= "string" or
    exploder.IgnoreWeaponList ~= false or exploder.ExplodeOnMunitionImpact ~= true or
    type(effects) ~= "table" or type(effects.FX) ~= "table" then return end
local target = false
for _, fx in pairs(effects.FX) do
    if type(fx) == "table" and fx.Type == "SOUND" and fx.Event == "ExploderExplode" and
        fx.Name == "target_bullseye" then target = true end
end
if not target then return end
-- gmatch skips empty fields, while the native parser can consume them. Reject
-- malformed separators/NULs before counting slots or appending anything.
local list = exploder.WeaponList
if string.sub(list, 1, 1) == "|" or string.sub(list, -1) == "|" or
    string.find(list, "||", 1, true) or string.find(list, "\000", 1, true) then return end
local known = {}
for weapon in string.gmatch(exploder.WeaponList, "[^|]+") do known[weapon] = true end
-- Ordinary LR pistol/tomahawk targets only. Cannon and Gatling puzzles retain
-- their native lists; keep native collision, destruction, rewards and scoring.
if not known.pstl or not known.toma then return end
-- ExploderData has eight FourCC slots. The native parser does not bounds-check
-- writes: never append the entire projectile set to this string. A marker enables
-- the verified native impact gate, which preserves the original message/data.
if known.lctg then return end
local count = 0
for weapon in string.gmatch(exploder.WeaponList, "[^|]+") do
    if #weapon ~= 4 then return end
    count = count + 1
end
if count >= 8 then
    log("SKIPPED: ordinary target has no free native weapon slot")
    return
end
exploder.WeaponList = exploder.WeaponList .. "|lctg"
log("APPLIED: ordinary Lone Ranger targets use bounded native projectile gate; native scoring retained")
)lua";
inline constexpr std::string_view characterMountsPatch = R"lua(
local _, options, log = ...
local gadgets = type(DNA) == "table" and DNA.AgentGadgets
if type(gadgets) ~= "table" or type(gadgets.Avatar) ~= "table" or
    type(gadgets.Rider) ~= "table" or type(gadgets.Identification) ~= "table" then return end
local name = gadgets.Identification.ActorTypeMask
if not LiteralCrab.has("IsRegisteredAvatar") then return end
if type(name) ~= "string" or not LiteralCrab.IsRegisteredAvatar(name) then
    -- Identification masks and actor creation keys differ for several families
    -- (AV_Perry vs PNF_Perry, AV_LoneRanger vs LR_LoneRanger, etc.).
    name = gadgets.Identification.Name
    if type(name) ~= "string" or not LiteralCrab.IsRegisteredAvatar(name) then return end
end
-- Toy Box avatar DNA omits SummonMount. Keep the avatar's model/movement DNA
-- and add only the native, lock-checked hitching-post definitions.
if gadgets.SummonMount ~= nil and type(gadgets.SummonMount) ~= "table" then return end
local summon = gadgets.SummonMount or {}
if summon.TypedMounts ~= nil and type(summon.TypedMounts) ~= "table" then return end
local typed = summon.TypedMounts or {}
local defaults = {
    LR_HitchingPost = {
        {CreationKey = "LR_Silver", Lock = "RC_TOY_LR_Silver"},
        {CreationKey = "LR_Scout", Lock = "RC_LR_Scout"}
    },
    LR_MuleHitchingPost = {{CreationKey = "LR_Mule", Lock = "RC_LR_Mule"}},
    LR_ElephantHitchingPost = {{CreationKey = "LR_Elephant", Lock = "RC_LR_Elephant"}},
    IN_HitchingPost = {
        {CreationKey = "IN_MRINC_CAR", Lock = "RC_IN_MRINC_CAR"},
        {CreationKey = "INC_MrInc_SportsCar", Lock = "RC_INC_MrInc_SportsCar"}
    },
    IN_HelicopterHitchingPost = {{CreationKey = "INC_Helicopter_Bomb", Lock = "RC_TOY_SUPER_HERO_HELICOPTER"}},
    MU_HitchingPost = {
        {CreationKey = "MU_Bicycle", Lock = "RC_MU_BIKE"},
        {CreationKey = "MU_Bicycle_Mini", Lock = "RC_MU_BIKE_MINI"},
        {CreationKey = "MU_Bicycle_Sully", Lock = "RC_MU_BIKE_SULLEY"},
        {CreationKey = "MU_Bicycle_Randall", Lock = "RC_MU_BIKE_RANDY"}
    },
    TS_HitchingPost = {{CreationKey = "AV_Bullseye", Lock = "RC_BULLSEYE"}},
    TS_AlienHorseHitchingPost = {{CreationKey = "AlienHorse", Lock = "RC_ROBO_BULLSEYE"}}
}
-- Native default lists differ by avatar (Scout/Silver, Sulley's bike, etc.).
-- Reuse one only for its matching post type, never summon a car at a horse post.
-- An unknown custom fallback is left entirely under its original mod's control.
if summon.Mounts ~= nil then
    if type(summon.Mounts) ~= "table" then return end
    if #summon.Mounts > 0 then
        local matched = false
        for post, mounts in pairs(defaults) do
            local belongs = true
            for _, original in ipairs(summon.Mounts) do
                local known = false
                for _, candidate in ipairs(mounts) do
                    if type(original) == "table" and original.CreationKey == candidate.CreationKey then known = true end
                end
                if not known then belongs = false end
            end
            if belongs then defaults[post] = summon.Mounts; matched = true end
        end
        if not matched then return end
    end
end
local count = 0
for post, mounts in pairs(defaults) do
    if typed[post] == nil then typed[post] = mounts; count = count + 1 end
end
summon.TypedMounts = typed
gadgets.SummonMount = summon
if count > 0 then log("APPLIED: " .. name .. " supports " .. count .. " additional native hitching-post types; locks preserved") end
)lua";
inline constexpr std::string_view characterPreloadsPatch = R"lua(
local _, options, log = ...
if type(DataMap) ~= "table" then return end
if not LiteralCrab.has("IsRegisteredAvatar") then
    log("UNAVAILABLE: avatar preload registration adapter")
    return
end
local function include(record, name)
    if type(record.Includes) ~= "table" then record.Includes = {} end
    for _, value in ipairs(record.Includes) do if value == name then return end end
    record.Includes[#record.Includes + 1] = name
end
-- Registering a dependency map only exposes records. The active playset must load them.
local playsets = DataMap.Playset
local common = type(playsets) == "table" and playsets._loadforallplaysets_
if type(common) == "table" then
    include(common, "avatarstats|Include")
    include(common, "literalcrab_avatar_fx|Include")
    -- Projectiles are actors. Toy Story includes tool_pixarball; the Toy Box group
    -- supplies the original models, DNA and scripts for all avatar tools.
    include(common, "avatartools|Include")
    -- Avatar customization is loaded per zone, independently from actors and stats.
    -- These original packages contain the Classic/Exclusive prebuilt definitions.
    -- The engine's normal GameDB loader merges them; preserve every original file.
    if type(common.Files) ~= "table" then common.Files = {} end
    local seen = {}
    for _, file in ipairs(common.Files) do
        if type(file) == "string" then seen[string.lower(file)] = true end
    end
    for _, package in ipairs({"Core", "ToyBox", "Cars", "Incredibles", "LoneRanger", "Monsters", "Pirates", "ToyStory"}) do
        local file = "GameDB|GameDB/" .. package .. "/cust_Avatar__lua.lua|1|cust"
        if not seen[string.lower(file)] then
            common.Files[#common.Files + 1] = file
            seen[string.lower(file)] = true
        end
    end
    -- Native Toy Box vehicle audio supplies visitor engines, body sounds and
    -- dialogue rules. Keep Audio records so the engine resolves language/lifetime.
    for _, file in ipairs({"Audio|RumpusVehicles.fev", "Audio|RumpusCarBody.fev",
        "Audio|VehicleEngine.fev", "Audio|CARS_TBScript.dlg"}) do
        if not seen[string.lower(file)] then
            common.Files[#common.Files + 1] = file
            seen[string.lower(file)] = true
        end
    end
    log("APPLIED: common playset preload includes original avatar stats, motion FX and tool actors")
    log("APPLIED: original avatar customization packages available across playsets")
    log("APPLIED: original vehicle engine/body audio and Cars dialogue rules preloaded across playsets")
end
local groups = DataMap.Include
if type(groups) ~= "table" or type(groups.avatarstats) ~= "table" then return end
local files, seenFiles, visited = {}, {}, {}
local function collect(group, name)
    local key = group .. "|" .. name
    if visited[key] then return end
    visited[key] = true
    local records = DataMap[group]
    local record = type(records) == "table" and records[name]
    if type(record) ~= "table" then return end
    for _, file in ipairs(record.Files or {}) do
        if type(file) == "string" and string.lower(string.sub(file, 1, 4)) == "cho|" then
            local canonical = string.lower(file)
            if not seenFiles[canonical] then
                files[#files + 1] = file
                seenFiles[canonical] = true
            end
        end
    end
    for _, dependency in ipairs(record.Includes or {}) do
        if type(dependency) == "string" then
            local child, kind = string.match(dependency, "^([^|]+)|([^|]+)")
            if child and (kind == "Include" or kind == "Actor") then collect(kind, child) end
        end
    end
end
if type(DataMap.Actor) == "table" then
    for name in pairs(DataMap.Actor) do
        if LiteralCrab.IsRegisteredAvatar(name) then collect("Actor", name) end
    end
end
collect("Include", "avatartools")
-- Preload effect choreography only; keep character models and behavior assets streamed.
groups.literalcrab_avatar_fx = {Files = files}
log("APPLIED: " .. #files .. " original avatar/tool choreographies available to every playset")
)lua";
inline constexpr std::string_view characterCustomizationTracePatch = R"lua(
local _, options, log = ...
if type(Data) ~= "table" or Data.Type ~= "Customization" or type(Data.Databases) ~= "table" then return end
for _, database in ipairs(Data.Databases) do
    if type(database) == "table" and database.DBName == "Avatar" then
        local records = database.Records
        local prebuilt = type(records) == "table" and records.Prebuilt
        if type(prebuilt) == "table" then
            for _, record in ipairs(prebuilt) do
                if type(record) == "table" and type(record.name) == "string" then
                    log("AVATAR MODEL DEFINITION: " .. record.name .. " uid=" .. tostring(record.UID))
                end
            end
        end
    end
end
)lua";
inline constexpr std::string_view characterDependenciesTracePatch = R"lua(
local _, options, log = ...
if type(DataMap) ~= "table" then return end
local includes = DataMap.Include
local avatarstats = type(includes) == "table" and includes.avatarstats
local files = type(avatarstats) == "table" and avatarstats.Files
if type(files) == "table" then
    log("DEPENDENCIES: avatarstats has " .. #files .. " files")
    for _, file in ipairs(files) do log("STAT FILE: " .. tostring(file)) end
end
local fx = type(includes) == "table" and includes.literalcrab_avatar_fx
local tools = type(includes) == "table" and includes.avatartools
if type(tools) == "table" and type(tools.Includes) == "table" then
    log("DEPENDENCIES: avatar tools has " .. #tools.Includes .. " actor includes")
end
if type(fx) == "table" and type(fx.Files) == "table" then
    log("DEPENDENCIES: avatar FX has " .. #fx.Files .. " choreographies")
end
local playsets = DataMap.Playset
if type(playsets) == "table" then
    for name, playset in pairs(playsets) do
        if type(playset) == "table" and type(playset.Includes) == "table" then
            for _, dependency in ipairs(playset.Includes) do
                log("PLAYSET INCLUDE: " .. tostring(name) .. " " .. tostring(dependency))
            end
        end
    end
end
)lua";
inline constexpr std::string_view characterStatsTracePatch = R"lua(
local _, options, log = ...
local records = Data and Data.Records
if type(records) ~= "table" then return end
for _, record in pairs(records) do
    if type(record) == "table" and (record.UID == 173 or record.UID == 318) then
        log("STAT LOADED: package=" .. tostring(record.Package) .. " scope=" .. tostring(record.ScopeUID) .. " uid=" .. tostring(record.UID))
    end
end
)lua";
inline constexpr std::string_view characterNativeActorHomesPatch = R"lua(
local _, options, log = ...
if not LiteralCrab.has("RegisterPlaysetAvatars") then return end
if type(Data) ~= "table" or Data.Type ~= "ActorList" or type(Data.Records) ~= "table" or
    type(options.Playset) ~= "string" then return end
local count = 0
for _, actor in pairs(Data.Records) do
    if type(actor) == "table" and actor.Type == "Avatar" and type(actor.Name) == "string" and
        string.lower(string.sub(actor.Name, -3)) == "_ps" then
        -- Native actor tables can contain a _PS variant omitted from the PC
        -- selection roster. Keep its home separate from public membership.
        LiteralCrab.RegisterPlaysetAvatars(options.Playset, string.sub(actor.Name, 1, -4))
        count = count + 1
    end
end
if count > 0 then log("APPLIED: native actor variants registered for " .. options.Playset) end
)lua";
inline constexpr std::string_view characterToyboxActorsPatch = R"lua(
local _, options, log = ...
if not LiteralCrab.has("RegisterPlaysetAvatars") then return end
local actors = Data and Data.Records
if type(actors) ~= "table" or Data.Type ~= "ActorList" then return end
local count = 0
for _, actor in pairs(actors) do
    if type(actor) == "table" and actor.Type == "Avatar" and type(actor.Name) == "string" then
        LiteralCrab.RegisterPlaysetAvatars("RumpusRoom", actor.Name)
        count = count + 1
    end
end
log("APPLIED: registered " .. count .. " avatar variants from actor data")
)lua";
inline constexpr std::string_view characterPlaysetsPatch = R"lua(
local _, options, log = ...
local playsets = ZoneList and ZoneList.Records and ZoneList.Records.PlaysetProperties
if type(playsets) ~= "table" then return end
if not LiteralCrab.has("RegisterPlaysetAvatars") then
    log("UNAVAILABLE: character ownership adapter")
    return
end
local changed = 0
for _, name in ipairs({"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"}) do
    local playset = playsets[name]
    if type(playset) == "table" and type(playset.PlayableAvatars) == "string" then
        -- Vaults/progression still see the exact game's/mod's membership list.
        LiteralCrab.RegisterPlaysetAvatars(name, playset.PlayableAvatars)
        -- Tool particles/projectiles brought from the Toy Box need its FX resources.
        playset.WantToyBoxFX = true
        -- Motion-event choreography is separately filtered as "bloat" FX.
        playset.WantBloatFX = true
        -- CarEnergy still checks whether the actor is a local controlled car.
        -- This world gate must allow the native nitro HUD for visiting Cars.
        playset.ShowCarEnergyMeterUI = true
        -- Incredibles disables the vehicle tool selector at the world level.
        -- Visiting Cars still use their native tool/unlock/input checks.
        if name == "Incredibles" and playset.DisableVehicleToolSelect == true then
            playset.DisableVehicleToolSelect = false
            log("APPLIED: visiting Cars can select vehicle tools in Incredibles")
        end
        changed = changed + 1
    end
end
-- Register this last: Toy Box contains native playset avatars as well as exclusives.
local toybox = playsets.RumpusRoom
if type(toybox) == "table" and type(toybox.PlayableAvatars) == "string" then
    LiteralCrab.RegisterPlaysetAvatars("RumpusRoom", toybox.PlayableAvatars)
end
log("APPLIED: " .. changed .. " play sets keep native membership and enable tool/motion FX")
)lua";
}
