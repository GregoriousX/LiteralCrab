// SPDX-License-Identifier: GPL-3.0-only
// Minimal read-only ZIP support for asset mods: ordinary archives (stored or deflated, as any
// common tool writes them) and the game's own. No ZIP64, encryption or spanning.
#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::mods::zip {
struct Entry {
    std::string name;  // As stored, '/' separated.
    std::uint16_t method{};
    std::uint16_t time{}, date{};
    std::uint32_t crc{}, compressedSize{}, size{};
    std::uint32_t localHeader{};
    bool directory() const noexcept { return !name.empty() && name.back() == '/'; }
};

class Archive {
public:
    // Throws std::runtime_error with a reason when the file is not a supported ZIP.
    explicit Archive(const std::wstring& path);
    Archive(Archive&& other) noexcept;
    Archive& operator=(Archive&&) = delete;
    Archive(const Archive&) = delete;
    ~Archive();
    const std::wstring& path() const noexcept { return file; }
    const std::vector<Entry>& entries() const noexcept { return list; }
    // Exact name match, ignoring ASCII case and '\' versus '/'. Null when absent.
    const Entry* find(std::string_view name) const noexcept;
    // Uncompressed member data; verifies the CRC.
    std::string read(const Entry& entry) const;
private:
    void readAt(std::uint64_t offset, void* buffer, std::size_t size) const;
    std::uint64_t dataOffset(const Entry& entry) const;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring file;
    std::vector<Entry> list;
};

std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc = 0) noexcept;
// Raw deflate (RFC 1951) to exactly size bytes. Throws on malformed input.
std::string inflate(std::string_view compressed, std::uint32_t size);
}
