// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <array>

namespace crabe::di1 {
// DI1 particledata v12 has right-aligned directories immediately before its
// 20-byte footer. Recipes and envelope data use offsets from the original base.
struct ParticleDirectory {
    const unsigned char* data{};
    std::size_t size{}, footer{}, begin[3]{};
    std::uint16_t count[3]{}, curves{};
    std::uint32_t bodyEnd{}, envelopes{};
    static constexpr std::size_t stride[3] = {16, 8, 8};
    template<class T> static T read(const unsigned char* p) noexcept {
        T value{}; std::memcpy(&value, p, sizeof(value)); return value;
    }
    bool parse(const void* source, std::size_t length) noexcept {
        *this = {};
        if (!source || length < 20 || length > 64 * 1024 * 1024) return false;
        data = static_cast<const unsigned char*>(source); size = length; footer = size - 20;
        const auto* end = data + footer;
        if (read<std::uint16_t>(end) != 0x123 || read<std::uint16_t>(end + 2) != 12) return false;
        bodyEnd = read<std::uint32_t>(end + 4); envelopes = read<std::uint32_t>(end + 8);
        curves = read<std::uint16_t>(end + 12);
        std::size_t bytes = 0;
        for (unsigned kind = 0; kind < 3; ++kind) {
            count[kind] = read<std::uint16_t>(end + 14 + kind * 2);
            bytes += count[kind] * stride[kind];
        }
        if (bytes > footer || bodyEnd > envelopes || envelopes > footer - bytes ||
            curves * std::size_t(8) > footer - bytes - envelopes) return false;
        std::size_t position = footer - bytes;
        for (unsigned kind = 0; kind < 3; ++kind) {
            begin[kind] = position;
            for (unsigned i = 0; i < count[kind]; ++i)
                if (read<std::uint32_t>(data + position + i * stride[kind]) >= bodyEnd) return false;
            position += count[kind] * stride[kind];
        }
        return true;
    }
    const unsigned char* row(unsigned kind, unsigned index) const noexcept {
        return data + begin[kind] + index * stride[kind];
    }
};

// Validate all rows before writing; malformed input remains intact. The native
// registry predicate must not mutate this buffer or throw. Compact backwards:
// each destination is at or above its source, so unread rows remain untouched.
// Preserve all recipe offsets, material indices, curve indices, size and footer
// position. Native release identifies owned recipes by their pointer range.
template<class Keep> bool filterParticleDirectory(void* source, std::size_t size, Keep keep) noexcept {
    ParticleDirectory view;
    if (!view.parse(source, size)) return false;
    auto* output = static_cast<unsigned char*>(source);
    std::size_t destination = view.footer;
    std::uint16_t counts[3]{};
    for (int kind = 2; kind >= 0; --kind)
        for (unsigned i = view.count[kind]; i > 0; --i) {
                const auto* row = view.row(kind, i - 1);
                const auto offset = ParticleDirectory::read<std::uint32_t>(row);
                const auto id = ParticleDirectory::read<std::uint32_t>(row + 4);
                if (!keep(kind, id, offset)) continue;
                destination -= ParticleDirectory::stride[kind];
                std::memmove(output + destination, row, ParticleDirectory::stride[kind]);
                ++counts[kind];
        }
    std::memcpy(output + view.footer + 14, counts, sizeof(counts));
    return true;
}

// Native v12 import patches six ushort curve references per emitter and the
// byte-counted ushort list at particle recipe+108. Drop unreferenced envelopes
// and remap only retained recipes, before the engine adds its global base index.
inline bool compactParticleEnvelopes(void* source, std::size_t size) noexcept {
    ParticleDirectory view;
    if (!view.parse(source, size) || view.curves >= 20000) return false;
    std::array<std::uint16_t, 20000> mapping;
    mapping.fill(0xFFFF);
    const auto unique = [&view](unsigned kind, unsigned index) {
        const auto offset = view.read<std::uint32_t>(view.row(kind, index));
        for (unsigned i = 0; i < index; ++i)
            if (view.read<std::uint32_t>(view.row(kind, i)) == offset) return false;
        return true;
    };
    // Validate every reference first. Effects have no import-time curve patch.
    for (unsigned kind : {0u, 2u})
        for (unsigned i = 0; i < view.count[kind]; ++i) {
            const auto offset = view.read<std::uint32_t>(view.row(kind, i));
            const unsigned start = kind == 0 ? 0x108 : 0;
            if (offset > view.bodyEnd || start > view.bodyEnd - offset) return false;
            const unsigned count = kind == 0 ? view.data[offset + 0x105] : 6;
            if (count * 2u > view.bodyEnd - offset - start) return false;
            for (unsigned j = 0; j < count; ++j) {
                const auto id = view.read<std::uint16_t>(view.data + offset + start + j * 2);
                if (id == 0xFFFF) continue;
                if (id >= view.curves) return false;
                mapping[id] = 0;
            }
        }
    std::uint16_t count = 0;
    for (unsigned i = 0; i < view.curves; ++i) if (mapping[i] == 0) mapping[i] = count++;
    auto* output = static_cast<unsigned char*>(source);
    for (unsigned kind : {0u, 2u})
        for (unsigned i = 0; i < view.count[kind]; ++i) {
            if (!unique(kind, i)) continue;
            const auto offset = view.read<std::uint32_t>(view.row(kind, i));
            const unsigned start = kind == 0 ? 0x108 : 0;
            const unsigned references = kind == 0 ? view.data[offset + 0x105] : 6;
            for (unsigned j = 0; j < references; ++j) {
                auto* reference = output + offset + start + j * 2;
                const auto id = view.read<std::uint16_t>(reference);
                if (id != 0xFFFF) std::memcpy(reference, &mapping[id], 2);
            }
        }
    for (unsigned i = 0; i < view.curves; ++i)
        if (mapping[i] != 0xFFFF)
            std::memmove(output + view.envelopes + mapping[i] * 8, output + view.envelopes + i * 8, 8);
    std::memcpy(output + view.footer + 12, &count, 2);
    return true;
}
}
