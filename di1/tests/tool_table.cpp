// SPDX-License-Identifier: GPL-3.0-only
#include "di1/tool_table.hpp"
#include <array>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

using namespace crabe::di1;
namespace {
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
constexpr unsigned none = ~0u;
unsigned word(std::string_view bytes, std::size_t at) {
    if (at + 4 > bytes.size()) throw std::runtime_error("test word out of bounds");
    unsigned value{};
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<unsigned char>(bytes[at + i]) << (i * 8);
    return value;
}
void put(std::string& bytes, std::size_t at, unsigned value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<char>(value >> (i * 8));
}
void append(std::string& bytes, unsigned value) {
    const auto at = bytes.size(); bytes.resize(at + 4); put(bytes, at, value);
}
struct Parsed {
    std::array<std::string, 13> blocks;
    explicit Parsed(std::string_view bytes) {
        std::size_t at{};
        for (auto& block : blocks) { const auto size = word(bytes, at); block = bytes.substr(at, size); at += size; }
        CHECK(at == bytes.size());
    }
    std::string text(unsigned index) const {
        const auto strings = std::string_view(blocks[0]).substr(4);
        return std::string(strings.substr(index, strings.find('\0', index) - index));
    }
    unsigned tool(unsigned index, unsigned field) const { return word(blocks[11], 8 + index * 76 + field * 4); }
    unsigned actor(unsigned index, unsigned field) const { return word(blocks[10], 8 + index * 64 + field * 4); }
    std::vector<unsigned> list(unsigned start) const {
        std::vector<unsigned> result;
        while (word(blocks[1], 4 + start * 4) != none) result.push_back(word(blocks[1], 4 + start++ * 4));
        return result;
    }
    std::vector<unsigned> profile(unsigned index) const { return list(word(blocks[12], 12 + index * 8)); }
};
struct Fixture {
    std::array<std::string, 13> blocks;
    std::string strings;
    Fixture() {
        for (auto& block : blocks) block.assign(4, '\0');
        // Nontrivial opaque condition/effect blocks must be preserved exactly.
        for (unsigned i = 2; i < 10; ++i) blocks[i] += "opaque-" + std::to_string(i);
        blocks[10].resize(8 + 2 * 64, '\0'); put(blocks[10], 4, 2);
        for (unsigned i = 0; i < 2; ++i) {
            const std::string name = i ? "ExistingDescriptor" : "BlasterDescriptor";
            const auto at = 8 + i * 64;
            put(blocks[10], at, text(name)); put(blocks[10], at + 4, nativeToolHash(name));
            put(blocks[10], at + 8, text(i ? "tool_existing" : "tool_blaster"));
            for (unsigned j = 3; j < 16; ++j) put(blocks[10], at + j * 4, 0x10000u + i * 100 + j);
        }
        const auto actorList = list({0});
        blocks[11].resize(8 + 5 * 76, '\0'); put(blocks[11], 4, 5);
        const char* names[] = {"ITEM_NONE", "ITEM_BLASTER", "PROP_NONE", "ABILITY_NONE", "ITEM_EXISTING"};
        for (unsigned i = 0; i < 5; ++i) {
            const auto at = 8 + i * 76;
            put(blocks[11], at, text(names[i])); put(blocks[11], at + 4, nativeToolHash(names[i]));
            put(blocks[11], at + 8, text(i == 2 ? "Props" : i == 3 ? "Abilities" : "Items"));
            put(blocks[11], at + 12, text("UP_Blaster"));
            put(blocks[11], at + 20, text(names[i])); put(blocks[11], at + 24, actorList);
            for (unsigned j = 7; j < 19; ++j) put(blocks[11], at + j * 4, 0x10000u + i * 100 + j);
        }
        blocks[12].resize(8 + 3 * 8, '\0'); put(blocks[12], 4, 3);
        const char* profiles[] = {"AV_Buzz", "AV_Zurg", "NPC"};
        for (unsigned i = 0; i < 3; ++i) {
            put(blocks[12], 8 + i * 8, text(profiles[i]));
            put(blocks[12], 12 + i * 8, list(i ? std::vector<unsigned>{0, 1, 2, 3} : std::vector<unsigned>{0, 1, 4, 2, 3}));
        }
    }
    unsigned text(const std::string& value) { const auto at = static_cast<unsigned>(strings.size()); strings += value; strings += '\0'; return at; }
    unsigned list(const std::vector<unsigned>& values) {
        const auto start = static_cast<unsigned>((blocks[1].size() - 4) / 4);
        for (auto value : values) append(blocks[1], value); append(blocks[1], none); return start;
    }
    std::string finish() {
        blocks[0] = std::string(4, '\0') + strings;
        std::string raw;
        for (auto& block : blocks) { put(block, 0, static_cast<unsigned>(block.size())); raw += block; }
        return raw;
    }
};
ToolTableEntry definition() { return {"LCR_TEST_GUN", "ITEM_BLASTER", "lcr_test_actor", {"av_buzz"}, "ITEM_EXISTING"}; }
void preserved(const Parsed& before, const Parsed& after, const std::vector<unsigned>& changed) {
    for (unsigned i = 0; i < 13; ++i) {
        if (i == 0 || i == 1) CHECK(after.blocks[i].substr(4, before.blocks[i].size() - 4) == before.blocks[i].substr(4));
        else if (i == 10 || i == 11) CHECK(after.blocks[i].substr(8, before.blocks[i].size() - 8) == before.blocks[i].substr(8));
        else if (i == 12) {
            CHECK(after.blocks[i].size() == before.blocks[i].size());
            CHECK(after.blocks[i].substr(0, 8) == before.blocks[i].substr(0, 8));
            for (unsigned p = 0; p < word(before.blocks[i], 4); ++p) {
                CHECK(after.blocks[i].substr(8 + p * 8, 4) == before.blocks[i].substr(8 + p * 8, 4));
                bool expected{}; for (auto value : changed) if (p == value) expected = true;
                if (!expected) CHECK(after.blocks[i].substr(8 + p * 8, 8) == before.blocks[i].substr(8 + p * 8, 8));
            }
        } else CHECK(after.blocks[i] == before.blocks[i]);
    }
}
void appendAndInheritance() {
    auto raw = Fixture{}.finish(); const auto original = raw;
    Parsed before(raw), after(extendToolTable(raw, {definition()}));
    CHECK(raw == original);
    CHECK(nativeToolHash("ITEM_BLASTER") == 0x3a1287e3u);
    CHECK(nativeToolHash("") == 0u);
    CHECK(nativeToolHash("LCR_Test") != nativeToolHash("lcr_test"));
    CHECK(word(after.blocks[10], 4) == 3 && word(after.blocks[11], 4) == 6);
    preserved(before, after, {0});
    CHECK(after.text(after.tool(5, 0)) == "LCR_TEST_GUN");
    CHECK(after.tool(5, 1) == nativeToolHash("LCR_TEST_GUN"));
    CHECK(after.tool(5, 5) == after.tool(5, 0));
    CHECK(after.list(after.tool(5, 6)) == std::vector<unsigned>({2}));
    CHECK(after.text(after.actor(2, 0)) == "lcr_test_actor");
    CHECK(after.actor(2, 0) == after.actor(2, 2));
    CHECK(after.actor(2, 1) == nativeToolHash("lcr_test_actor"));
    for (unsigned j = 3; j < 16; ++j) CHECK(after.actor(2, j) == before.actor(0, j));
    for (unsigned j = 2; j < 19; ++j) {
        if (j == 5 || j == 6) continue;
        CHECK(after.tool(5, j) == before.tool(j == 9 || j == 10 || j == 12 ? 4 : 1, j));
    }
    CHECK(after.profile(0) == std::vector<unsigned>({0, 1, 4, 5, 2, 3}));
    CHECK(after.profile(1) == before.profile(1) && after.profile(2) == before.profile(2));
    // Without profile declarations the native boss and all player profiles stay
    // byte-identical. Custom avatars may opt in through their private streams.
    auto isolated = definition(); isolated.inventoryProfiles.clear(); isolated.controlSource.clear();
    Parsed privateOnly(extendToolTable(raw, {isolated})); preserved(before, privateOnly, {});
    CHECK(privateOnly.blocks[12] == before.blocks[12]);
    for (unsigned j = 2; j < 19; ++j) if (j != 5 && j != 6) CHECK(privateOnly.tool(5, j) == before.tool(1, j));
    isolated.modelHandle="ControlGunHandle";
    Parsed fitted(extendToolTable(raw,{isolated}));preserved(before,fitted,{});
    CHECK(fitted.text(fitted.actor(2,4))=="ControlGunHandle");
    for(unsigned field=3;field<16;++field) if(field!=4) CHECK(fitted.actor(2,field)==before.actor(0,field));
    CHECK(fitted.text(fitted.tool(5,0))==privateOnly.text(privateOnly.tool(5,0)));
    for(unsigned field=1;field<19;++field) if(field!=5) CHECK(fitted.tool(5,field)==privateOnly.tool(5,field));
    auto prop = definition(); prop.id = "LCR_PROP"; prop.actor = "LCR_PROP_ACTOR"; prop.baseTool = "PROP_NONE"; prop.controlSource.clear();
    Parsed props(extendToolTable(raw, {prop})); CHECK(props.profile(0) == std::vector<unsigned>({0, 1, 4, 2, 5, 3}));
    auto second = definition(); second.id += "2"; second.actor += "2";
    Parsed multiple(extendToolTable(raw, {definition(), second}));
    CHECK(multiple.profile(0) == std::vector<unsigned>({0, 1, 4, 5, 6, 2, 3}));
    // A personal tool, like ITEM_BUZZ, drops only the base's upgrade (+0C) and
    // zone list (+10); every other inherited field stays the base tool's.
    auto personal = isolated; personal.personal = true;
    Parsed own(extendToolTable(raw, {personal})); preserved(before, own, {});
    CHECK(own.tool(5, 3) == none && own.tool(5, 4) == none);
    for (unsigned field = 1; field < 19; ++field) if (field != 3 && field != 4 && field != 5) CHECK(own.tool(5, field) == fitted.tool(5, field));
    CHECK(extendToolTable(raw, {}) == raw);
    // Native actor variants can share descriptor names/hashes; the base tool's
    // explicit actor index identifies the right attachment variant.
    auto repeated = Fixture{};
    put(repeated.blocks[10], 8 + 64, word(repeated.blocks[10], 8));
    put(repeated.blocks[10], 12 + 64, word(repeated.blocks[10], 12));
    const auto repeatedRaw = repeated.finish();
    Parsed repeatedAfter(extendToolTable(repeatedRaw, {definition()}));
    CHECK(repeatedAfter.actor(2, 3) == word(repeated.blocks[10], 8 + 3 * 4));
}
void failuresAreTransactional() {
    const auto raw = Fixture{}.finish(); const auto original = raw;
    const auto reject = [&](std::string bytes, ToolTableEntry entry) {
        bool rejected{};
        try { (void)extendToolTable(bytes, {entry}); } catch (const std::runtime_error& error) {
            rejected = std::string(error.what()).find("custom tool table:") == 0;
        }
        CHECK(rejected); CHECK(raw == original);
    };
    for (const auto& badId : {"ITEM_BLASTER", "OTHER_CUSTOM", "LCR_", "LCR_bad/path"}) {
        auto bad = definition(); bad.id = badId; reject(raw, bad);
    }
    auto bad = definition(); bad.actor = "tool_blaster"; reject(raw, bad);
    bad = definition(); bad.baseTool = "missing"; reject(raw, bad);
    bad = definition(); bad.controlSource = "PROP_NONE"; reject(raw, bad);
    bad = definition(); bad.modelHandle="../bad";reject(raw,bad);
    bad = definition(); bad.modelHandle=std::string(97,'A');reject(raw,bad);
    bad = definition(); bad.inventoryProfiles = {"missing"}; reject(raw, bad);
    bad = definition(); bad.inventoryProfiles = {"AV_Buzz", "av_buzz"}; reject(raw, bad);
    reject(raw.substr(0, raw.size() - 1), definition()); reject(raw + "trailing", definition());
    auto fixture = Fixture{}; put(fixture.blocks[11], 4, 100); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[11], 12 + 76, 1); reject(fixture.finish(), definition()); // wrong native hash
    fixture = Fixture{}; put(fixture.blocks[11], 8 + 76 + 24, 0xffffu); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[11], 8 + 76 + 24, fixture.list({0, 1})); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[11], 8 + 76 + 24, fixture.list({99})); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[12], 12, fixture.list({0, 2, 1, 3})); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[12], 12, fixture.list({0, 1, 1, 2, 3})); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[12], 12, fixture.list({2, 3})); reject(fixture.finish(), definition());
    fixture = Fixture{}; put(fixture.blocks[12], 12, fixture.list({99})); reject(fixture.finish(), definition());
    fixture = Fixture{}; fixture.strings.append(65536, 'x'); reject(fixture.finish(), definition());
    const auto once = extendToolTable(raw, {definition()}); reject(once, definition());
    bad = definition(); bad.id = "lcr_test_gun"; bad.actor += "other"; reject(once, bad);
    bad = definition(); bad.id += "other"; reject(once, bad);
    bool rejected{};
    try { (void)extendToolTable(raw, {definition(), definition()}); } catch (const std::runtime_error&) { rejected = true; }
    CHECK(rejected); CHECK(raw == original);
}
void originalRegression(const char* path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("original tool table argument cannot be opened");
    const std::string raw((std::istreambuf_iterator<char>(input)), {});
    ToolTableEntry entry{"LCR_NATIVE_REGRESSION", "ITEM_BLASTER", "lcr_native_regression_actor", {"AV_Buzz"}, {}};
    Parsed before(raw), after(extendToolTable(raw, {entry}));
    const auto oldTools = word(before.blocks[11], 4), oldActors = word(before.blocks[10], 4);
    CHECK(word(after.blocks[11], 4) == oldTools + 1 && word(after.blocks[10], 4) == oldActors + 1);
    std::vector<unsigned> changed;
    for (unsigned i = 0; i < word(before.blocks[12], 4); ++i) {
        if (before.text(word(before.blocks[12], 8 + i * 8)) == "AV_Buzz") {
            changed.push_back(i); auto expected = before.profile(i);
            auto insert = expected.begin();
            while (insert != expected.end() && before.text(before.tool(*insert, 2)) == "Items") ++insert;
            expected.insert(insert, oldTools); CHECK(after.profile(i) == expected);
        } else CHECK(after.profile(i) == before.profile(i));
    }
    CHECK(changed.size() == 1); preserved(before, after, changed);
    entry.inventoryProfiles.clear(); Parsed isolated(extendToolTable(raw, {entry})); preserved(before, isolated, {});
    CHECK(isolated.blocks[12] == before.blocks[12]);
    entry.modelHandle="ControlGunHandle";
    Parsed fitted(extendToolTable(raw,{entry}));preserved(before,fitted,{});
    CHECK(fitted.text(fitted.actor(oldActors,4))=="ControlGunHandle");
    for(unsigned field=3;field<16;++field) if(field!=4)
        CHECK(fitted.actor(oldActors,field)==isolated.actor(oldActors,field));
    entry.modelHandle.clear();
    // A player control donor alone cannot make an NPC-only base tool selectable:
    // the availability/unlock fields intentionally stay with the selected base.
    entry.baseTool = "ITEM_ZURG_BOSS_BLASTER"; entry.controlSource = "ITEM_BLASTER";
    Parsed boss(extendToolTable(raw, {entry}));
    unsigned bossIndex = none, playerIndex = none;
    for (unsigned i = 0; i < oldTools; ++i) {
        const auto name = before.text(before.tool(i, 0));
        if (name == entry.baseTool) bossIndex = i;
        if (name == entry.controlSource) playerIndex = i;
    }
    CHECK(bossIndex != none && playerIndex != none);
    if (bossIndex != none && playerIndex != none) {
        CHECK(boss.tool(oldTools, 3) == before.tool(bossIndex, 3));
        for (unsigned field : {17u, 18u}) CHECK(boss.tool(oldTools, field) == before.tool(bossIndex, field));
        for (unsigned field : {9u, 10u, 12u}) CHECK(boss.tool(oldTools, field) == before.tool(playerIndex, field));
    }
}
}
int main(int argc, char** argv) {
    try { appendAndInheritance(); failuresAreTransactional(); if (argc > 1) originalRegression(argv[1]); }
    catch (const std::exception& error) { std::printf("ERROR: %s\n", error.what()); ++failures; }
    if (!failures) std::puts("Native tool table extension, selection inheritance, inventory isolation and rejection checks passed.");
    return failures ? 1 : 0;
}
