// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace crabe::di1 {
inline constexpr std::string_view characterResetFilterPatch = R"lua(
local _, options, log = ...
if type(Data) ~= "table" or Data.Package ~= "Core" or type(Data.Sets) ~= "table" then return end
local actors
for _, set in ipairs(Data.Sets) do
    if type(set) == "table" and set.set == "ActorType" then
        if actors then return end
        actors = set
    end
end
if not actors then return end
local definitions, terminal, target, biped = {}, {}, nil, nil
for _, name in ipairs({"masks", "filters", "metafilters"}) do
    if type(actors[name]) ~= "table" then return end
    for _, row in ipairs(actors[name]) do
        if type(row) ~= "table" then return end
        local key = name == "masks" and row.mask or row.filter
        if type(key) ~= "string" or definitions[key] then return end
        definitions[key] = row
        terminal[key] = name == "masks"
        if key == "PC_RespawnDeath" then target = row end
        if key == "AV_AvatarBiped" then biped = row end
    end
end
-- This reset patch retains NPC membership. A separate progression patch widens
-- the six biped avatar groups after validating their original definitions.
local original = "AV_AvatarPirates|MG_Gibbs|MG_Pintel|MG_Ragetti|MG_TDalma|PC_PirateAny|Blockhead_PIR"
if not target or target.UID ~= 3736 or target.desc ~= original or
    not biped or biped.UID ~= 144 then return end
local function expand(key, leaves, visiting, depth)
    local row = definitions[key]
    if not row or visiting[key] or depth > 32 then return false end
    if terminal[key] then leaves[key] = true; return true end
    if type(row.desc) ~= "string" or row.desc == "" or
        string.find(row.desc, "[^%w_|]") or string.sub(row.desc, 1, 1) == "|" or
        string.sub(row.desc, -1) == "|" or string.find(row.desc, "||", 1, true) then return false end
    visiting[key] = true
    for child in string.gmatch(row.desc, "[^|]+") do
        if not expand(child, leaves, visiting, depth + 1) then return false end
    end
    visiting[key] = nil
    return true
end
local existing, visitors = {}, {}
if not expand("PC_RespawnDeath", existing, {}, 0) or
    not expand("AV_AvatarBiped", visitors, {}, 0) then return end
-- These shipped biped declarations are helpers/NPCs/intro models, not the
-- supported visiting figures. Cars retain their distinct native reset policy.
local excluded = {AV_Proxy=true, AV_Blockhead=true, AV_Spark=true, AV_Sparkman=true, AV_Frozone=true}
local additions = {}
for key in pairs(visitors) do
    if not existing[key] and not excluded[key] then additions[#additions + 1] = key end
end
if #additions == 0 then return end
table.sort(additions)
target.desc = original .. "|" .. table.concat(additions, "|")
log("APPLIED: Pirates reset filter includes visiting biped figures; original NPC membership retained")
)lua";
inline constexpr std::string_view characterTriggerFiltersPatch = R"lua(
local _, options, log = ...
if type(Data) ~= "table" or Data.Package ~= "Core" or type(Data.Sets) ~= "table" then return end
local actors
for _, set in ipairs(Data.Sets) do
    if type(set) == "table" and set.set == "ActorType" then
        if actors then return end
        actors = set
    end
end
if not actors then return end
local definitions, terminal = {}, {}
for _, kind in ipairs({"masks", "filters", "metafilters"}) do
    if type(actors[kind]) ~= "table" then return end
    for _, row in ipairs(actors[kind]) do
        if type(row) ~= "table" then return end
        local key = kind == "masks" and row.mask or row.filter
        if type(key) ~= "string" or definitions[key] then return end
        definitions[key], terminal[key] = row, kind == "masks"
    end
end
-- Vehicle/intro/friend filters describe roles, not biped playset membership.
-- Add flat leaves, never cyclic references between the widened avatar groups.
local targets = {
    {"AV_AvatarIncredibles", 64, "AV_Dash|AV_ElastiGirl|AV_Frozone|AV_MrIncredible|AV_Syndrome|AV_Violet"},
    {"AV_AvatarLoneRanger", 146, "AV_LoneRanger|AV_Tonto"},
    {"AV_AvatarMonsters", 192, "AV_Mike|AV_Randall|AV_Sully"},
    {"AV_AvatarPirates", 215, "AV_Barbossa|AV_DavyJones|AV_JackSparrow"},
    {"AV_AvatarToyBox", 2935, "AV_Anna|AV_Elsa|AV_JackSkellington|AV_MickeyMouse|AV_Perry|AV_Phineas|AV_Ralph|AV_Rapunzel|AV_Vanellope"},
    {"AV_AvatarToyStory", 263, "AV_Blockhead|AV_Buzz|AV_Jessie|AV_Woody|AV_Zurg"},
}
local biped, cars = definitions.AV_AvatarBiped, definitions.AV_AvatarCars
if not biped or biped.UID ~= 144 or not cars or cars.UID ~= 255 then return end
local function expand(key, result, visiting, depth)
    local row = definitions[key]
    if not row or visiting[key] or depth > 32 then return false end
    if terminal[key] then result[key] = true; return true end
    if type(row.desc) ~= "string" or row.desc == "" or string.find(row.desc, "[^%w_|]") or
        string.sub(row.desc, 1, 1) == "|" or string.sub(row.desc, -1) == "|" or
        string.find(row.desc, "||", 1, true) then return false end
    visiting[key] = true
    for child in string.gmatch(row.desc, "[^|]+") do
        if not expand(child, result, visiting, depth + 1) then return false end
    end
    visiting[key] = nil
    return true
end
local visitors, vehicles = {}, {}
if not expand("AV_AvatarBiped", visitors, {}, 0) or
    not expand("AV_AvatarCars", vehicles, {}, 0) then return end
local excluded = {AV_Proxy=true, AV_Blockhead=true, AV_Spark=true, AV_Sparkman=true, AV_Frozone=true}
for key in pairs(visitors) do
    if vehicles[key] then return end
    if excluded[key] then visitors[key] = nil end
end
local changes = {}
for _, spec in ipairs(targets) do
    local row = definitions[spec[1]]
    if not row or row.UID ~= spec[2] then return end
    local original, additions = {}, {}
    for key in string.gmatch(spec[3], "[^|]+") do
        if not terminal[key] then return end
        original[key] = true
    end
    for key in pairs(visitors) do if not original[key] then additions[#additions + 1] = key end end
    table.sort(additions)
    local wanted = spec[3] .. (#additions > 0 and "|" .. table.concat(additions, "|") or "")
    -- Validate every target before mutation. Accept our exact output on reload.
    if row.desc ~= spec[3] and row.desc ~= wanted then return end
    if row.desc ~= wanted then changes[#changes + 1] = {row, wanted} end
end
for _, change in ipairs(changes) do change[1].desc = change[2] end
if #changes > 0 then log("APPLIED: six avatar progression filters accept visiting biped figures; Cars/mount/vehicle filters retained") end
)lua";
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
-- Private custom figures are not in any play set's roster; the loader names
-- their player actor types so they get the same hitching posts.
local custom = type(options) == "table" and type(options.Custom) == "table" and options.Custom or {}
local function known(value)
    return type(value) == "string" and (custom[value] == true or LiteralCrab.IsRegisteredAvatar(value))
end
if not known(name) then
    -- Identification masks and actor creation keys differ for several families
    -- (AV_Perry vs PNF_Perry, AV_LoneRanger vs LR_LoneRanger, etc.).
    name = gadgets.Identification.Name
    if not known(name) then return end
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

// The companion supplies the appended binary tool row and private tool actor;
// the hidden native megaphone ability and ordinary combat stay unchanged.
inline constexpr std::string_view characterScareActorPatch = R"lua(
local _, options, log = ...
if type(Data) ~= "table" or Data.Type ~= "ActorList" or type(Data.Records) ~= "table" then return end
local source
for _, row in pairs(Data.Records) do
    if type(row) == "table" then
        if row.Name == "tool_literalcrabmegaphone" then return end
        if row.Name == "tool_megaphone" and row.Parms == "DNAFile=characters/Megaphone.dnax;" and
            row.Type == "Other" and row.SubType == "ItemsAndProps" then source = row end
    end
end
if not source then return end
Data.Records[#Data.Records + 1] = {Name="tool_literalcrabmegaphone", Type=source.Type,
    SubType=source.SubType, Parms="DNAFile=characters/LiteralCrab_Megaphone.dnax;"}
log("APPLIED: separate experimental megaphone actor registered")
)lua";
inline constexpr std::string_view characterScareDependenciesPatch = R"lua(
local _, options, log = ...
if type(DataMap) ~= "table" or type(DataMap.Actor) ~= "table" or
    type(DataMap.Actor.tool_megaphone) ~= "table" then return end
local key = "tool_literalcrabmegaphone"
if DataMap.Actor[key] ~= nil then return end
local original = DataMap.Actor.tool_blaster
if type(original) ~= "table" or type(original.Files) ~= "table" then return end
local files = {}
for _, file in ipairs(original.Files) do
    if type(file) ~= "string" then return end
    local lower = string.lower(file)
    if lower == "dna|characters/blaster.dnax" then
        file = "DNA|characters/LiteralCrab_Megaphone.dnax"
    elseif lower == "entity|characters/avatar_tools/blaster" then
        file = "Entity|characters/Avatar_Tools/MU_Megaphone"
    elseif lower == "script|characters/blaster.sx" then
        file = "Script|characters/LiteralCrab_Megaphone.sx"
    end
    files[#files + 1] = file
end
-- The tool script requests Mike's original scare audio and green
-- choreography. A visitor's own character does not preload those resources.
files[#files + 1] = "Audio|MU_Mike.fev"
files[#files + 1] = "Cho|choreographies/mu_scare_green/mu_scare_green"
DataMap.Actor[key] = {Files=files}
local common = type(DataMap.Playset) == "table" and DataMap.Playset._loadforallplaysets_
if type(common) == "table" then
    if type(common.Includes) ~= "table" then common.Includes = {} end
    local include = key .. "|Actor"
    for _, old in ipairs(common.Includes) do if old == include then return end end
    common.Includes[#common.Includes + 1] = include
end
log("APPLIED: experimental megaphone dependencies available")
)lua";
// A distinct weapon name prevents the tool cleanup from removing a native
// Mike/Sulley combo weapon already present in the avatar's inventory.
inline constexpr std::string_view characterScareWeaponPatch = R"lua(
local _, options, log = ...
if type(weapon) ~= "table" then return end
local source
for _, row in ipairs(weapon) do
    if type(row) == "table" then
        if row.name == "LCRMUScare" then return end
        if row.name == "SullyComboX" and row.class == "gun" and
            row.typeCC == "sulx" and row.projectile == "SullyComboX" then source = row end
    end
end
if not source then return end
local added = {}
for key, value in pairs(source) do added[key] = value end
added.name = "LCRMUScare"
weapon[#weapon + 1] = added
log("APPLIED: isolated avatar-owned MU scare weapon; original combo weapons preserved")
)lua";
inline constexpr std::string_view characterScareIconPatch = R"lua(
local _, options, log = ...
if type(ToolSelect) ~= "table" or type(ToolSelect.GetIconPath) ~= "function" then return end
local original = ToolSelect.GetIconPath
function ToolSelect:GetIconPath(name)
    if name == "ITEM_LITERALCRAB_MEGAPHONE" then return original(self, "ITEM_MIKE") end
    return original(self, name)
end
)lua";

}
