// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string_view>

namespace crabe::di1 {
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
    log("APPLIED: common playset preload includes original avatar stats, motion FX and tool actors")
    log("APPLIED: original avatar customization packages available across playsets")
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
