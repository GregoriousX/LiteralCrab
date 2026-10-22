// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <deque>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crabe::di1 {
// Keep the equipped tool's unique identity; reuse the game's localized skill
// label only at dictionary lookup, including languages supplied by other mods.
inline const char* scareToolLabelKey(const char* key) noexcept {
    return key && std::string_view(key) == "ITEM_LITERALCRAB_MEGAPHONE_name" ? "ITEM_MIKE_name" : key;
}
inline bool scareToolPoseMessage(const char* message, std::uint32_t sender,
    std::uint32_t receiver, const char* equipped) noexcept {
    return message && std::string_view(message) == "LCRPose" && sender && receiver && sender != receiver &&
        equipped && std::string_view(equipped) == "ITEM_LITERALCRAB_MEGAPHONE";
}
// The original zone lists describe ownership and progression, not just permission to enter.
// Keep a separate copy for choosing the actor variant; never append to the game's lists.
class CharacterPlaysets {
    struct Entry { std::string avatar, playset; };
    std::vector<Entry> entries;
    struct Actor { std::string key, name; };
    // Actor creation can retain the selected name. Append-only deque storage
    // keeps returned strings alive when subsequent actor lists are registered.
    std::deque<Actor> baseActors;
    static std::string lower(std::string_view value) {
        std::string result(value);
        for (auto& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return result;
    }
    static std::string canonicalAvatar(std::string_view value) {
        auto result = lower(value);
        if (result.ends_with("_ps")) result.resize(result.size() - 3);
        if (result.ends_with("infinite")) result.resize(result.size() - 8);
        if (result == "av_francesco") result = "av_cars_francesco";
        return result;
    }
public:
    static bool playsetVariant(std::string_view avatar) {
        return avatar.size() >= 3 && lower(avatar.substr(avatar.size() - 3)) == "_ps";
    }
    bool registered(std::string_view avatar) const {
        const auto name = canonicalAvatar(avatar);
        for (const auto& entry : entries) if (entry.avatar == name) return true;
        return false;
    }
    void add(std::string_view playset, std::string_view avatars) {
        const auto home = lower(playset);
        while (!avatars.empty()) {
            const auto comma = avatars.find(',');
            auto name = avatars.substr(0, comma);
            while (!name.empty() && name.front() == ' ') name.remove_prefix(1);
            while (!name.empty() && name.back() == ' ') name.remove_suffix(1);
            if (!name.empty()) {
                // Keep exact registered creation keys separately from canonical
                // ownership. Crystal/base aliases share a home, but not a model.
                if (!playsetVariant(name)) {
                    auto key = lower(name);
                    bool known = false;
                    for (const auto& actor : baseActors) if (actor.key == key) { known = true; break; }
                    if (!known) baseActors.push_back({std::move(key), std::string(name)});
                }
                // Crystal actor records and base roster names share one home, regardless
                // of which table or variant loads first.
                auto canonical = canonicalAvatar(name);
                bool found = false;
                for (auto& entry : entries) if (entry.avatar == canonical) {
                    // Actor tables can load before the zone list. A specific native home
                    // always takes precedence over registration from the Toy Box.
                    if (entry.playset == "rumpusroom" && home != "rumpusroom") entry.playset = home;
                    found = true; break;
                }
                if (!found) entries.push_back({std::move(canonical), home});
            }
            if (comma == std::string_view::npos) break;
            avatars.remove_prefix(comma + 1);
        }
    }
    bool foreign(std::string_view avatar, std::string_view playset) const {
        const auto name = canonicalAvatar(avatar), current = lower(playset);
        if (current == "rumpusroom" || current == "miscellaneous" || current.empty()) return false;
        for (const auto& entry : entries) {
            if (name == entry.avatar) return current != entry.playset;
        }
        // Unknown/modded actors retain the engine's own suffix and fallback behavior.
        return false;
    }
    const char* visitorBaseActor(std::string_view avatar, std::string_view playset) const {
        if (!playsetVariant(avatar) || !foreign(avatar, playset)) return nullptr;
        const auto key = lower(avatar.substr(0, avatar.size() - 3));
        // A suffixed name alone is insufficient: never invent an actor or turn
        // an unregistered crystal/custom variant into a different character.
        for (const auto& actor : baseActors) if (actor.key == key) return actor.name.c_str();
        return nullptr;
    }
};
}
