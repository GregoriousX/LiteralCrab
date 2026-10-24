// SPDX-License-Identifier: GPL-3.0-only
// DI1 binary scene tuples (format 1.1), used by BENT entities, OCT scenes and
// BANM blends. Atoms keep their raw element words and string indices, so an
// unmodified scene re-encodes byte for byte; only atoms a caller changes are
// rewritten. PC little-endian only. Unknown atom kinds are rejected.
#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::di1::scene {
struct Atom {
    std::uint16_t depth{}, kind{}, name{};
    std::vector<std::uint32_t> values; // Raw elements: string indices, integers or float bits.
};
struct Scene {
    std::vector<std::string> strings;
    std::vector<Atom> atoms; // Pre-order; atoms[0] is the root.
};

Scene decode(std::string_view bytes);
std::string encode(const Scene& scene);
// Index of the first equal string, appending it when absent.
std::uint16_t intern(Scene& scene, std::string_view text);
std::string_view name(const Scene& scene, const Atom& atom);
// Scalar string atoms (for example SceneFilename or Filename); empty otherwise.
bool isText(const Atom& atom) noexcept;
std::string_view text(const Scene& scene, const Atom& atom);
void setText(Scene& scene, Atom& atom, std::string_view value);
// One past the last atom of the subtree starting at index.
std::size_t subtreeEnd(const Scene& scene, std::size_t index);
// Direct children of the atom at index.
std::vector<std::size_t> children(const Scene& scene, std::size_t index);
// First direct child with the given atom name, or npos.
std::size_t child(const Scene& scene, std::size_t index, std::string_view atomName);
constexpr std::size_t npos = static_cast<std::size_t>(-1);
}
