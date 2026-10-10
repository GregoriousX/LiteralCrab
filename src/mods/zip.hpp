// SPDX-License-Identifier: GPL-3.0-only
// Minimal ZIP support for asset mods: reads ordinary archives (stored or deflated, as any common
// tool writes them) and the game's own layout, and writes the game's layout. The engine's
// archives are standard ZIPs with a copy of the end record and central directory at offset 0
// and an MD5 of each member's data in a "KF" extra field. No ZIP64, encryption or spanning.
#pragma once
#include <windows.h>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace crabe::mods::zip {
using Md5 = std::array<std::uint8_t, 16>;

struct Entry {
    std::string name;  // As stored, '/' separated.
    std::uint16_t method{};
    std::uint16_t time{}, date{};
    std::uint32_t crc{}, compressedSize{}, size{};
    std::uint32_t localHeader{};
    bool hasMd5{};
    Md5 md5{};
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
    // Streams the member's stored (possibly compressed) bytes in chunks.
    void copyRaw(const Entry& entry, const std::function<void(const void*, std::size_t)>& sink) const;
    // Uncompressed member data; verifies the CRC.
    std::string read(const Entry& entry) const;
private:
    void readAt(std::uint64_t offset, void* buffer, std::size_t size) const;
    std::uint64_t dataOffset(const Entry& entry) const;
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::wstring file;
    std::vector<Entry> list;
};

// One member of an archive being written.
struct Member {
    std::string name;
    std::uint16_t method{};  // 0 stored, 8 deflated
    std::uint32_t crc{}, compressedSize{}, size{};
    Md5 md5{};
    // Writes exactly compressedSize bytes through the sink.
    std::function<void(const std::function<void(const void*, std::size_t)>&)> write;
};

// Writes members in the game's layout to path (via a temporary file renamed on success).
void writeGameArchive(const std::wstring& path, const std::vector<Member>& members,
    std::uint16_t time, std::uint16_t date);

std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc = 0) noexcept;
Md5 md5(const void* data, std::size_t size);
// Raw deflate (RFC 1951) to exactly size bytes. Throws on malformed input.
std::string inflate(std::string_view compressed, std::uint32_t size);
}
