// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::di1 {
struct ToolTableEntry {
    std::string id, baseTool, actor;
    std::vector<std::string> inventoryProfiles;
    // Optional same-group targeting, controls and HUD donor. The base tool's
    // actors, attachment, unlock, zone and selection rules remain its own.
    std::string controlSource;
    // Optional native attachment control exported by the custom model.
    std::string modelHandle;
};

// ToolSetInfo::CalcHash, including its case-sensitive native byte convention.
std::uint32_t nativeToolHash(std::string_view name) noexcept;

// Read-only view of the effective table, used to infer defaults.
struct ToolRowInfo {
    std::uint32_t index{}, hash{};
    std::string id, group;
    std::vector<std::string> actors; // Spawned actor names of the tool's descriptors.
    // Items need native control contexts (Allow_Gun, ...) to be usable by a
    // player. NPC-only rows, such as boss weapons, have none.
    bool playerControls{};
};
struct ToolTableInfo {
    std::vector<ToolRowInfo> tools;
    std::vector<std::pair<std::string, std::vector<std::uint32_t>>> profiles;
    // Exact case-sensitive tool ID, as the native table resolves it.
    const ToolRowInfo* find(std::string_view id) const noexcept;
    // Profiles are keyed by ActorTypeMask; matching ignores ASCII case.
    const std::vector<std::uint32_t>* profile(std::string_view name) const noexcept;
};
ToolTableInfo readToolTable(std::string_view table);

// Extend the effective native table at startup without changing existing actor
// or tool records, old index streams or unselected inventory profiles. IDs and
// actor names must belong to LCR_. Throws before returning any partial result.
// The first version supports a base tool with exactly one actor descriptor.
std::string extendToolTable(std::string_view original, const std::vector<ToolTableEntry>& definitions);
}
