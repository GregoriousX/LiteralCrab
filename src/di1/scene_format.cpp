// SPDX-License-Identifier: GPL-3.0-only
#include "di1/scene_format.hpp"
#include <cstring>
#include <stdexcept>

namespace crabe::di1::scene {
namespace {
[[noreturn]] void reject(const std::string& reason) { throw std::runtime_error("scene: " + reason); }
struct Layout { unsigned count, width; bool string, container; };
// kind -> count bytes, element bytes, string reference. Matches the native reader.
bool layout(unsigned kind, Layout& out) {
    switch (kind) {
    case 0x01: out = {0, 0, false, true}; return true;
    case 0x05: case 0x0B: out = {0, 2, true, false}; return true;
    case 0x0A: out = {1, 2, true, false}; return true;
    case 0x4A: out = {2, 2, true, false}; return true;
    case 0x12: out = {1, 4, false, false}; return true;
    case 0x13: out = {0, 4, false, false}; return true;
    case 0x52: out = {2, 4, false, false}; return true;
    case 0x1A: case 0x23: out = {1, 1, false, false}; return true;
    case 0x1B: out = {0, 1, false, false}; return true;
    case 0x5A: case 0x63: out = {2, 1, false, false}; return true;
    case 0xA3: out = {3, 1, false, false}; return true;
    case 0x11A: out = {1, 2, false, false}; return true;
    case 0x11B: out = {0, 2, false, false}; return true;
    case 0x15A: out = {2, 2, false, false}; return true;
    case 0x21A: out = {1, 3, false, false}; return true;
    case 0x21B: out = {0, 3, false, false}; return true;
    case 0x31B: out = {0, 4, false, false}; return true;
    default: return false;
    }
}
std::uint32_t read(std::string_view bytes, std::size_t at, unsigned width) {
    std::uint32_t value{};
    for (unsigned i = 0; i < width; ++i) value |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (8 * i);
    return value;
}
void write(std::string& out, std::uint32_t value, unsigned width) {
    for (unsigned i = 0; i < width; ++i) out += static_cast<char>(value >> (8 * i));
}
constexpr unsigned char header[12] = {0x29, 0x76, 0x01, 0x45, 0xcd, 0xcc, 0x8c, 0x3f, 0, 0, 0, 0};
}

Scene decode(std::string_view bytes) {
    if (bytes.size() < 60 || std::memcmp(bytes.data(), header, 12) != 0) reject("expected little-endian format 1.1");
    const auto strings = read(bytes, 12, 4), tree = read(bytes, 16, 4), count = read(bytes, 20, 4);
    for (std::size_t i = 24; i < 60; ++i) if (bytes[i]) reject("unexpected header fields");
    if (60ull + strings + tree != bytes.size() || !strings || bytes[59 + strings] != '\0') reject("invalid section sizes");
    Scene scene;
    for (std::size_t at = 60, end = 60 + strings; at < end;) {
        const auto stop = bytes.find('\0', at);
        scene.strings.emplace_back(bytes.substr(at, stop - at));
        at = stop + 1;
    }
    if (scene.strings.size() > 65536) reject("too many strings");
    std::size_t at = 60 + strings;
    while (at < bytes.size()) {
        if (bytes.size() - at < 4) reject("truncated atom header");
        const auto flags = read(bytes, at, 2);
        Atom atom;
        atom.depth = static_cast<std::uint16_t>(flags / 0x400);
        atom.kind = static_cast<std::uint16_t>(flags % 0x400);
        atom.name = static_cast<std::uint16_t>(read(bytes, at + 2, 2));
        at += 4;
        Layout shape{};
        if (!layout(atom.kind, shape)) reject("unsupported atom kind");
        if (atom.name >= scene.strings.size()) reject("invalid atom name");
        if (!shape.container) {
            std::size_t elements = 1;
            if (shape.count) {
                if (bytes.size() - at < shape.count) reject("truncated atom count");
                elements = read(bytes, at, shape.count); at += shape.count;
            }
            if (elements > (bytes.size() - at) / shape.width) reject("truncated atom payload");
            for (std::size_t i = 0; i < elements; ++i, at += shape.width) {
                const auto value = read(bytes, at, shape.width);
                if (shape.string && value >= scene.strings.size()) reject("invalid string reference");
                atom.values.push_back(value);
            }
        }
        if (scene.atoms.empty() ? atom.depth != 0 : (atom.depth == 0 || atom.depth > scene.atoms.back().depth + 1))
            reject("invalid atom hierarchy");
        scene.atoms.push_back(std::move(atom));
    }
    if (scene.atoms.size() != count || scene.atoms.empty()) reject("atom count mismatch");
    return scene;
}

std::string encode(const Scene& scene) {
    std::string strings;
    for (const auto& text : scene.strings) { strings += text; strings += '\0'; }
    std::string tree;
    for (std::size_t i = 0; i < scene.atoms.size(); ++i) {
        const auto& atom = scene.atoms[i];
        Layout shape{};
        if (atom.depth > 63 || !layout(atom.kind, shape)) reject("unsupported atom");
        if ((i == 0) != (atom.depth == 0) || (i && atom.depth > scene.atoms[i - 1].depth + 1)) reject("invalid atom hierarchy");
        write(tree, atom.depth * 0x400u + atom.kind, 2);
        write(tree, atom.name, 2);
        if (shape.container) continue;
        if (shape.count) {
            if (atom.values.size() >= (1ull << (8 * shape.count))) reject("too many atom elements");
            write(tree, static_cast<std::uint32_t>(atom.values.size()), shape.count);
        } else if (atom.values.size() != 1) reject("scalar atom needs one value");
        for (const auto value : atom.values) write(tree, value, shape.width);
    }
    std::string result(reinterpret_cast<const char*>(header), 12);
    write(result, static_cast<std::uint32_t>(strings.size()), 4);
    write(result, static_cast<std::uint32_t>(tree.size()), 4);
    write(result, static_cast<std::uint32_t>(scene.atoms.size()), 4);
    result.append(36, '\0');
    return result + strings + tree;
}

std::uint16_t intern(Scene& scene, std::string_view value) {
    for (std::size_t i = 0; i < scene.strings.size(); ++i) if (scene.strings[i] == value) return static_cast<std::uint16_t>(i);
    if (scene.strings.size() >= 65536 || value.find('\0') != std::string_view::npos) reject("cannot add string");
    scene.strings.emplace_back(value);
    return static_cast<std::uint16_t>(scene.strings.size() - 1);
}
std::string_view name(const Scene& scene, const Atom& atom) { return scene.strings[atom.name]; }
bool isText(const Atom& atom) noexcept { return (atom.kind == 0x05 || atom.kind == 0x0B) && atom.values.size() == 1; }
std::string_view text(const Scene& scene, const Atom& atom) {
    return isText(atom) ? std::string_view(scene.strings[atom.values[0]]) : std::string_view{};
}
void setText(Scene& scene, Atom& atom, std::string_view value) {
    if (!isText(atom)) reject("not a string atom");
    atom.values[0] = intern(scene, value);
}
std::size_t subtreeEnd(const Scene& scene, std::size_t index) {
    const auto depth = scene.atoms.at(index).depth;
    auto end = index + 1;
    while (end < scene.atoms.size() && scene.atoms[end].depth > depth) ++end;
    return end;
}
std::vector<std::size_t> children(const Scene& scene, std::size_t index) {
    std::vector<std::size_t> result;
    const auto depth = scene.atoms.at(index).depth;
    for (auto at = index + 1, end = subtreeEnd(scene, index); at < end; ++at)
        if (scene.atoms[at].depth == depth + 1) result.push_back(at);
    return result;
}
std::size_t child(const Scene& scene, std::size_t index, std::string_view atomName) {
    for (const auto at : children(scene, index)) if (name(scene, scene.atoms[at]) == atomName) return at;
    return npos;
}
}
