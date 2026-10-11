// SPDX-License-Identifier: GPL-3.0-only
// [Gameplay] CharactersInAnyPlayset: play any character in any play set.
//
// Based on InfinityUnlocked by Philip Parkinson II, which patches the executable and replaces
// three data archives. This version changes the game only in memory and only where the play
// set restriction itself is decided, so mods keep working:
//
// GameManager::FindPlaysetForCharacter (0x48CB50) returns a character's home play set. The
// sites below compare it with the current world and reject a mismatch; each rejection branch
// is removed. The executable is never written and nothing changes while the option is off.
//   - System_IsCharacterValid, the scripts' gate: empty or different play set.
//   - Figure placement on the base: its play set validation block is skipped, as the game
//     already does in the Toy Box.
//   - Player character validity for the world: empty or different play set.
//   - Two characters from the same play set (character swaps): always the same.
//   - Zone entry: the character counts as valid, as in the Toy Box.
//   - Actor lists: a play set loads every play set's actor list, which the Toy Box always does
//     ("IsHolodecksPlayset"), so other characters' actors exist there.
// Left native on purpose: IsCarCharacter (InfinityUnlocked makes every character a car), the
// spawn sparkle choice, the PlayableAvatars list parser and the "go to play set" dialog.
//
// The zone list patch (playableAvatarsPatch) appends every character to each play set's
// PlayableAvatars, after its own characters. Character group bitmasks stay native: Cars
// activities (races, monster tires) are restricted through them.
#include "di1/character_freedom.hpp"
#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace crabe::di1 {
namespace {
    constexpr std::uint32_t findPlaysetRva = 0x8CB50;
    bool active{};

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
        {"same play set for a swap", 0x77E15B,
         "E8 ?? ?? ?? ?? 8B 00 50 8D 4C 24 10 C7 44 24 1C 00 00 00 00 E8 ?? ?? ?? ?? 8D 4C 24 20 C6 44 24 18 02 "
         "E8 ?? ?? ?? ?? 8B 54 24 24 8B 0D ?? ?? ?? ?? 52 8D 44 24 28 50 E8 ?? ?? ?? ?? 8B 00 50 8D 4C 24 24 "
         "C6 44 24 1C 03 E8 ?? ?? ?? ?? 8D 4C 24 24 C6 44 24 18 05 E8 ?? ?? ?? ?? 8D 4C 24 0C E8 ?? ?? ?? ?? "
         "8D 4C 24 20 E8 ?? ?? ?? ?? 8B 74 24 0C 83 7E F4 00 74 55 8B 7C 24 20 83 7F F4 00 74 4B 57 56 "
         "E8 ?? ?? ?? ?? 83 C4 08 85 C0 75 0E 56 57 E8 ?? ?? ?? ?? 83 C4 08 85 C0 74 2F",
         0x77E192, {{0x75, "74 55", "90 90"}, {0x7F, "74 4B", "90 90"}, {0x8D, "75 0E", "90 90"}, {0x9B, "74 2F", "90 90"}}},
        {"zone entry", 0x79A305,
         "E8 ?? ?? ?? ?? 8B 00 50 8D 4C 24 1C C6 44 24 30 05 E8 ?? ?? ?? ?? 8D 4C 24 20 C6 44 24 2C 04 "
         "E8 ?? ?? ?? ?? 68 ?? ?? ?? ?? 57 E8 ?? ?? ?? ?? 83 C4 08 85 C0 75 04 B3 01",
         0x79A305, {{0x34, "75 04", "90 90"}}},
        {"actor lists", 0x28B2BA, "8B 74 24 20 84 C0 75 08 85 F6 74 04 32 DB EB 02 B3 01",
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

const std::string_view playableAvatarsPatch = R"lua(
local _, options, log = ...
local playsets = ZoneList and ZoneList.Records and ZoneList.Records.PlaysetProperties
if type(playsets) ~= "table" then return end
-- Every playable character of DI1, by the names the zone list uses.
local everyone = {
    "AV_MrIncredible", "AV_ElastiGirl", "AV_Dash", "AV_Violet", "AV_Syndrome", "AV_Frozone",
    "PIR_JackSparrow", "PIR_Barbossa", "PIR_DavyJones", "MU_Sully", "MU_Mike", "MU_Randall",
    "LR_LoneRanger", "LR_Tonto", "AV_Buzz", "AV_Jessie", "AV_Woody", "AV_Zurg",
    "AV_McQueen", "AV_Holly", "AV_Mater", "AV_Francesco", "AV_Cars_Finn", "AV_Cars_Luigi",
    "AV_Cars_Ramone", "AV_Cars_Flo", "AV_Cars_ChickHicks", "AV_Cars_King", "AV_Cars_Carla",
    "AV_Cars_Shu", "AV_Fillmore", "FRO_Anna", "FRO_Elsa", "NBC_JackSkellington",
    "TB_MickeyMouse", "PNF_Phineas", "PNF_Perry", "WR_Ralph", "WR_Vanellope", "TAN_Rapunzel",
}
local changed = 0
for _, name in ipairs({"Cars", "Incredibles", "LoneRanger", "MonstersU", "Pirates", "ToyStoryInSpace"}) do
    local playset = playsets[name]
    if type(playset) == "table" and type(playset.PlayableAvatars) == "string" then
        local seen, list = {}, {}
        -- The play set's own characters stay first, in their order.
        for avatar in string.gmatch(playset.PlayableAvatars, "[^,]+") do
            if not seen[avatar] then seen[avatar] = true; list[#list + 1] = avatar end
        end
        for _, avatar in ipairs(everyone) do
            if not seen[avatar] then seen[avatar] = true; list[#list + 1] = avatar end
        end
        playset.PlayableAvatars = table.concat(list, ",")
        changed = changed + 1
    end
end
log("APPLIED: " .. changed .. " play sets accept every character")
)lua";

bool installCharacterFreedom(HMODULE host, const Config& options) {
    if (!options.charactersInAnyPlayset) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    const char* actorPath = codeMatches(base + 0x28B305, "68 ?? ?? ?? ??") ? *reinterpret_cast<const char* const*>(base + 0x28B306) : nullptr;
    if (!actorPath || !readable(actorPath, 26) || std::memcmp(actorPath, "GameDB/Core/%s_actors.lua", 26)) {
        log("Gameplay: CharactersInAnyPlayset unavailable: actor list loader differs.");
        return false;
    }
    for (const auto& site : sites)
        if (!verified(base, site)) {
            log("Gameplay: CharactersInAnyPlayset unavailable: %s code differs; nothing changed.", site.name);
            return false;
        }
    unsigned count = 0;
    for (const auto& site : sites)
        for (const auto& edit : site.edits) {
            if (!edit.replacement) continue;
            unsigned char bytes[16];
            const auto length = parse(edit.replacement, bytes);
            if (!write(base + site.rva + edit.offset, bytes, length)) {
                log("Gameplay: CharactersInAnyPlayset: could not write %s; the game may be partly patched.", site.name);
                return false;
            }
            ++count;
        }
    log("Gameplay: characters allowed in every play set (%u native checks relaxed).", count);
    active = true;
    return true;
}

bool characterFreedomActive() noexcept { return active; }
}
