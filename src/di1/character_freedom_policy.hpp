// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crabe::di1 {
// The original zone lists describe ownership and progression, not just permission to enter.
// Keep a separate copy for choosing the actor variant; never append to the game's lists.
class CharacterPlaysets {
    struct Entry { std::string avatar, playset; };
    std::vector<Entry> entries;
    static std::string lower(std::string_view value) {
        std::string result(value);
        for (auto& c : result) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        return result;
    }
    static std::string canonicalAvatar(std::string_view value) {
        auto result = lower(value);
        if (result.ends_with("infinite")) result.resize(result.size() - 8);
        if (result == "av_francesco") result = "av_cars_francesco";
        return result;
    }
public:
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
};
}
