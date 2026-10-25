// SPDX-License-Identifier: GPL-3.0-only
#include "di1/tool_table.hpp"
#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>

namespace crabe::di1 {
std::uint32_t nativeToolHash(std::string_view name) noexcept {
    std::uint32_t value = name.empty() ? 0u : static_cast<unsigned char>(name.front()) << 7;
    for (const unsigned char ch : name) value = (value * 1000003u) ^ ch;
    return value ^ static_cast<std::uint32_t>(name.size());
}
namespace {
constexpr auto none = std::numeric_limits<std::uint32_t>::max();
using Actor = std::array<std::uint32_t, 16>;
using Tool = std::array<std::uint32_t, 19>;
using Profile = std::array<std::uint32_t, 2>;
[[noreturn]] void invalid(const std::string& reason) { throw std::runtime_error("custom tool table: " + reason); }
std::string lower(std::string value) {
    for (auto& ch : value) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
    return value;
}
bool ownIdentifier(std::string_view name) {
    if (name.size() < 5 || name.size() > 127 || lower(std::string(name.substr(0, 4))) != "lcr_") return false;
    return std::all_of(name.begin(), name.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '_';
    });
}
bool controlIdentifier(std::string_view name) {
    return name.size()<=96 && std::all_of(name.begin(),name.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
    });
}
std::uint32_t word(std::string_view bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) invalid("truncated word");
    std::uint32_t result{};
    for (unsigned i = 0; i < 4; ++i) result |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    return result;
}
void put(std::string& bytes, std::size_t at, std::uint32_t value) {
    if (at > bytes.size() || bytes.size() - at < 4) invalid("truncated output word");
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<char>(value >> (i * 8));
}
void append(std::string& bytes, std::uint32_t value) {
    const auto at = bytes.size(); bytes.resize(at + 4); put(bytes, at, value);
}
std::uint32_t checkedSize(std::size_t size) {
    if (size > none) invalid("generated table exceeds native size limit");
    return static_cast<std::uint32_t>(size);
}
template<std::size_t N> std::vector<std::array<std::uint32_t, N>> rows(std::string_view block) {
    const auto count = word(block, 4);
    if (count > 65535 || block.size() != 8ull + count * N * 4ull) invalid("unsupported row count or stride");
    std::vector<std::array<std::uint32_t, N>> result(count);
    for (unsigned i = 0; i < count; ++i) for (unsigned j = 0; j < N; ++j)
        result[i][j] = word(block, 8 + static_cast<std::size_t>(i) * N * 4 + j * 4);
    return result;
}
struct Table {
    std::array<std::string, 13> blocks;
    std::string strings;
    std::vector<std::uint32_t> values;
    std::vector<Actor> actors;
    std::vector<Tool> tools;
    std::vector<Profile> profiles;
    explicit Table(std::string_view original) {
        std::size_t at{};
        for (auto& block : blocks) {
            const auto size = word(original, at);
            if (size < 4 || size > original.size() - at) invalid("invalid binary block");
            block = original.substr(at, size); at += size;
        }
        if (at != original.size() || (blocks[1].size() - 4) % 4) invalid("expected 13 native blocks");
        strings = blocks[0].substr(4);
        for (std::size_t i = 4; i < blocks[1].size(); i += 4) values.push_back(word(blocks[1], i));
        actors = rows<16>(blocks[10]); tools = rows<19>(blocks[11]); profiles = rows<2>(blocks[12]);
        validateRecords(actors); validateRecords(tools);
        std::vector<std::string> names;
        for (const auto& profile : profiles) {
            const auto name = lower(text(profile[0]));
            if (std::find(names.begin(), names.end(), name) != names.end()) invalid("duplicate inventory profile: " + name);
            names.push_back(name);
            for (const auto index : list(profile[1], 512)) if (index >= tools.size()) invalid("invalid profile tool index");
        }
    }
    std::string text(std::uint32_t index) const {
        if (index >= strings.size() || index >= 65536) invalid("invalid direct string parameter");
        const auto end = strings.find('\0', index);
        if (end == std::string::npos) invalid("unterminated string parameter");
        return strings.substr(index, end - index);
    }
    template<std::size_t N> void validateRecords(const std::vector<std::array<std::uint32_t, N>>& records) const {
        for (const auto& row : records) {
            const auto name = text(row[0]);
            if (name.empty() || row[1] != nativeToolHash(name)) invalid("native actor/tool hash mismatch: " + name);
            // Native actor descriptors legitimately repeat names/hashes (e.g.
            // Blunderbuss) for distinct ownership/attachment variants. Their
            // explicit indices stay intact; only new identities must be unique.
        }
    }
    std::vector<std::uint32_t> list(std::uint32_t start, std::size_t maximum) const {
        if (start >= values.size()) invalid("invalid native index stream");
        std::vector<std::uint32_t> result;
        for (std::size_t i = start; i < values.size(); ++i) {
            if (values[i] == none) return result;
            if (result.size() >= maximum) invalid("unterminated or excessive native index stream");
            result.push_back(values[i]);
        }
        invalid("unterminated native index stream");
    }
    std::uint32_t addText(std::string_view name) {
        if (strings.size() >= 65536 || name.size() >= 65536 - strings.size()) invalid("string pool exceeds native direct parameter limit");
        const auto result = checkedSize(strings.size()); strings.append(name); strings += '\0'; return result;
    }
    std::uint32_t addList(const std::vector<std::uint32_t>& list) {
        const auto result = checkedSize(values.size());
        if (list.size() + values.size() >= none / 4) invalid("index stream exceeds native size limit");
        values.insert(values.end(), list.begin(), list.end()); values.push_back(none); return result;
    }
    unsigned findTool(const std::string& name) const {
        const auto key = lower(name);
        unsigned result = none;
        for (unsigned i = 0; i < tools.size(); ++i) if (lower(text(tools[i][0])) == key) {
            if (result != none) invalid("ambiguous base/control tool: " + name);
            result = i;
        }
        if (result == none) invalid("base/control tool missing: " + name);
        return result;
    }
    std::string group(unsigned index) const {
        const auto result = text(tools[index][2]);
        if (result != "Items" && result != "Props" && result != "Abilities") invalid("unsupported tool group: " + result);
        return result;
    }
    void add(const ToolTableEntry& definition) {
        if (!ownIdentifier(definition.id) || !ownIdentifier(definition.actor)) invalid("new tool IDs and actors must use LCR_ identifiers");
        if (!controlIdentifier(definition.modelHandle)) invalid("invalid model attachment handle");
        for (const auto& row : tools) if (lower(text(row[0])) == lower(definition.id) || row[1] == nativeToolHash(definition.id))
            invalid("tool ID/name hash already registered: " + definition.id);
        for (const auto& row : actors) if (lower(text(row[0])) == lower(definition.actor) || row[1] == nativeToolHash(definition.actor) ||
            lower(text(row[2])) == lower(definition.actor)) invalid("tool actor already registered: " + definition.actor);
        if (actors.size() == 65535 || tools.size() == 65535) invalid("native actor/tool table is full");
        const auto base = findTool(definition.baseTool);
        const auto category = group(base);
        auto tool = tools[base];
        const auto children = list(tool[6], actors.size() + 1);
        if (children.size() != 1 || children[0] >= actors.size()) invalid("base tool must have exactly one actor descriptor");
        auto actor = actors[children[0]];
        if (!definition.controlSource.empty()) {
            const auto control = findTool(definition.controlSource);
            if (group(control) != category) invalid("control source belongs to a different group");
            for (const unsigned field : {9u, 10u, 12u}) tool[field] = tools[control][field];
        }
        actor[0] = addText(definition.actor); actor[1] = nativeToolHash(definition.actor); actor[2] = actor[0];
        if (!definition.modelHandle.empty()) actor[4]=addText(definition.modelHandle);
        tool[0] = addText(definition.id); tool[1] = nativeToolHash(definition.id);
        tool[5] = tool[0]; // Native presentation/dictionary item identifier.
        // GetTool checks the upgrade named by +0C, then the zone list at +10.
        if (definition.personal) tool[3] = tool[4] = none;
        tool[6] = addList({checkedSize(actors.size())});
        const auto index = checkedSize(tools.size());
        actors.push_back(actor); tools.push_back(tool);
        std::vector<std::string> requested;
        for (const auto& name : definition.inventoryProfiles) {
            const auto key = lower(name);
            if (std::find(requested.begin(), requested.end(), key) != requested.end()) invalid("duplicate requested inventory profile: " + name);
            requested.push_back(key);
            auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto& row) { return lower(text(row[0])) == key; });
            if (found == profiles.end()) invalid("inventory profile missing: " + name);
            auto inventory = list((*found)[1], 512);
            if (inventory.size() == 512) invalid("inventory profile exceeds 512 tools: " + name);
            std::vector<std::string> runs;
            auto insert = inventory.size();
            bool matched{};
            for (std::size_t i = 0; i < inventory.size(); ++i) {
                if (std::find(inventory.begin(), inventory.begin() + static_cast<std::ptrdiff_t>(i), inventory[i]) !=
                    inventory.begin() + static_cast<std::ptrdiff_t>(i)) invalid("duplicate inventory tool: " + name);
                const auto current = group(inventory[i]);
                if (runs.empty() || runs.back() != current) {
                    if (std::find(runs.begin(), runs.end(), current) != runs.end()) invalid("inventory group is not contiguous: " + name);
                    runs.push_back(current);
                }
                if (current == category) { matched = true; insert = i + 1; }
            }
            // A native group's first entry supplies the default (usually NONE).
            // Do not invent a new group or change that first/default selection.
            if (!matched) invalid("inventory lacks base tool group: " + name);
            inventory.insert(inventory.begin() + static_cast<std::ptrdiff_t>(insert), index);
            (*found)[1] = addList(inventory);
        }
    }
    template<std::size_t N> static std::string serialize(const std::vector<std::array<std::uint32_t, N>>& records) {
        std::string result(8, '\0'); appendCount(result, records.size());
        for (const auto& row : records) for (auto value : row) append(result, value);
        put(result, 0, checkedSize(result.size())); return result;
    }
    static void appendCount(std::string& bytes, std::size_t count) { put(bytes, 4, checkedSize(count)); }
    std::string finish() {
        blocks[0] = std::string(4, '\0') + strings; put(blocks[0], 0, checkedSize(blocks[0].size()));
        blocks[1].assign(4, '\0'); for (auto value : values) append(blocks[1], value);
        put(blocks[1], 0, checkedSize(blocks[1].size()));
        blocks[10] = serialize(actors); blocks[11] = serialize(tools); blocks[12] = serialize(profiles);
        std::string result;
        for (const auto& block : blocks) result += block;
        checkedSize(result.size()); return result;
    }
};
}
const ToolRowInfo* ToolTableInfo::find(std::string_view id) const noexcept {
    for (const auto& tool : tools) if (tool.id == id) return &tool;
    return nullptr;
}
const std::vector<std::uint32_t>* ToolTableInfo::profile(std::string_view name) const noexcept {
    const auto key = lower(std::string(name));
    for (const auto& [profileName, rows] : profiles) if (lower(profileName) == key) return &rows;
    return nullptr;
}
ToolTableInfo readToolTable(std::string_view original) {
    const Table table(original);
    ToolTableInfo result;
    for (std::uint32_t i = 0; i < table.tools.size(); ++i) {
        const auto& row = table.tools[i];
        ToolRowInfo tool{i, row[1], table.text(row[0]), table.text(row[2]), {}, row[10] != none};
        // Only descriptor indices inside the actor block name spawned actors;
        // callers that need an actor require exactly one valid entry.
        if (row[6] != none) for (const auto actor : table.list(row[6], 512))
            tool.actors.push_back(actor < table.actors.size() ? table.text(table.actors[actor][2]) : std::string{});
        result.tools.push_back(std::move(tool));
    }
    for (const auto& profile : table.profiles) result.profiles.emplace_back(table.text(profile[0]), table.list(profile[1], 512));
    return result;
}

std::string extendToolTable(std::string_view original, const std::vector<ToolTableEntry>& definitions) {
    if (definitions.empty()) return std::string(original);
    Table table(original);
    for (const auto& definition : definitions) table.add(definition);
    return table.finish();
}
}
