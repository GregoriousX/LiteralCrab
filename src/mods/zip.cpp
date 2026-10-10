// SPDX-License-Identifier: GPL-3.0-only
#include "mods/zip.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
extern "C" {
#include "third_party/puff/puff.h"
}

namespace crabe::mods::zip {
namespace {
    constexpr std::uint32_t localSignature = 0x04034B50, centralSignature = 0x02014B50, endSignature = 0x06054B50;
    constexpr std::size_t chunk = 1 << 20;

    std::uint16_t u16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }
    std::uint32_t u32(const unsigned char* p) {
        return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
            static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
    }
    bool sameName(std::string_view left, std::string_view right) noexcept {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index) {
            char a = left[index] == '\\' ? '/' : left[index], b = right[index] == '\\' ? '/' : right[index];
            if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
            if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
            if (a != b) return false;
        }
        return true;
    }

    struct Crc {
        std::uint32_t table[256]{};
        constexpr Crc() {
            for (std::uint32_t index = 0; index < 256; ++index) {
                std::uint32_t value = index;
                for (int bit = 0; bit < 8; ++bit) value = value & 1 ? 0xEDB88320u ^ (value >> 1) : value >> 1;
                table[index] = value;
            }
        }
    };
    constexpr Crc crcTable;
}

std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t index = 0; index < size; ++index) crc = crcTable.table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

std::string inflate(std::string_view compressed, std::uint32_t size) {
    std::string output(size, '\0');
    unsigned char empty{};
    unsigned long outputLength = size, inputLength = static_cast<unsigned long>(compressed.size());
    const int status = puff(size ? reinterpret_cast<unsigned char*>(output.data()) : &empty, &outputLength,
        reinterpret_cast<const unsigned char*>(compressed.data()), &inputLength);
    if (status || outputLength != size) throw std::runtime_error("damaged deflate data");
    return output;
}

Archive::Archive(const std::wstring& path) : file(path) {
    handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_RANDOM_ACCESS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot open the archive");
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(handle, &length) || length.QuadPart < 22 || length.QuadPart >= 0xFFFFFFFFll) {
        CloseHandle(handle);
        throw std::runtime_error("not a ZIP archive or larger than 4 GiB");
    }
    try {
        const auto size = static_cast<std::uint64_t>(length.QuadPart);
        const auto tailSize = static_cast<std::size_t>((std::min<std::uint64_t>)(size, 22 + 0xFFFF));
        std::string tail(tailSize, '\0');
        readAt(size - tailSize, tail.data(), tailSize);
        const auto* bytes = reinterpret_cast<const unsigned char*>(tail.data());
        std::size_t end = std::string::npos;
        for (std::size_t at = tailSize - 22 + 1; at-- > 0;)
            if (u32(bytes + at) == endSignature && at + 22 + u16(bytes + at + 20) == tailSize) { end = at; break; }
        if (end == std::string::npos) throw std::runtime_error("not a ZIP archive");
        const auto count = u16(bytes + end + 10);
        const auto directorySize = u32(bytes + end + 12), directoryOffset = u32(bytes + end + 16);
        if (u16(bytes + end + 4) || u16(bytes + end + 6) || count != u16(bytes + end + 8))
            throw std::runtime_error("split archives are not supported");
        if (directoryOffset == 0xFFFFFFFF || static_cast<std::uint64_t>(directoryOffset) + directorySize > size)
            throw std::runtime_error("ZIP64 or damaged archive");
        std::string directory(directorySize, '\0');
        readAt(directoryOffset, directory.data(), directorySize);
        const auto* cursor = reinterpret_cast<const unsigned char*>(directory.data());
        const auto* limit = cursor + directorySize;
        list.reserve(count);
        for (std::uint32_t index = 0; index < count; ++index) {
            if (limit - cursor < 46 || u32(cursor) != centralSignature) throw std::runtime_error("damaged central directory");
            Entry entry;
            const auto flags = u16(cursor + 8);
            entry.method = u16(cursor + 10);
            entry.time = u16(cursor + 12);
            entry.date = u16(cursor + 14);
            entry.crc = u32(cursor + 16);
            entry.compressedSize = u32(cursor + 20);
            entry.size = u32(cursor + 24);
            const auto nameLength = u16(cursor + 28), extraLength = u16(cursor + 30), commentLength = u16(cursor + 32);
            entry.localHeader = u32(cursor + 42);
            if (limit - cursor < 46 + nameLength + extraLength + commentLength) throw std::runtime_error("damaged central directory");
            if (flags & 1) throw std::runtime_error("encrypted archives are not supported");
            entry.name.assign(reinterpret_cast<const char*>(cursor + 46), nameLength);
            if (entry.compressedSize == 0xFFFFFFFF || entry.size == 0xFFFFFFFF || entry.localHeader == 0xFFFFFFFF)
                throw std::runtime_error("ZIP64 members are not supported");
            list.push_back(std::move(entry));
            cursor += 46 + nameLength + extraLength + commentLength;
        }
    } catch (...) {
        CloseHandle(handle);
        handle = INVALID_HANDLE_VALUE;
        throw;
    }
}

Archive::Archive(Archive&& other) noexcept : handle(other.handle), file(std::move(other.file)), list(std::move(other.list)) {
    other.handle = INVALID_HANDLE_VALUE;
}

Archive::~Archive() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }

const Entry* Archive::find(std::string_view name) const noexcept {
    for (const auto& entry : list)
        if (sameName(entry.name, name)) return &entry;
    return nullptr;
}

void Archive::readAt(std::uint64_t offset, void* buffer, std::size_t size) const {
    auto* bytes = static_cast<char*>(buffer);
    while (size) {
        OVERLAPPED position{};
        position.Offset = static_cast<DWORD>(offset);
        position.OffsetHigh = static_cast<DWORD>(offset >> 32);
        const auto part = static_cast<DWORD>((std::min)(size, chunk));
        DWORD done{};
        if (!ReadFile(handle, bytes, part, &done, &position) || done != part)
            throw std::runtime_error("cannot read the archive");
        bytes += part;
        offset += part;
        size -= part;
    }
}

std::uint64_t Archive::dataOffset(const Entry& entry) const {
    unsigned char header[30];
    readAt(entry.localHeader, header, sizeof(header));
    if (u32(header) != localSignature) throw std::runtime_error("damaged member header: " + entry.name);
    return static_cast<std::uint64_t>(entry.localHeader) + 30 + u16(header + 26) + u16(header + 28);
}

std::string Archive::read(const Entry& entry) const {
    std::string raw(entry.compressedSize, '\0');
    if (!raw.empty()) readAt(dataOffset(entry), raw.data(), raw.size());
    std::string data;
    if (entry.method == 0) {
        if (entry.compressedSize != entry.size) throw std::runtime_error("damaged stored member: " + entry.name);
        data = std::move(raw);
    } else if (entry.method == 8) {
        data = inflate(raw, entry.size);
    } else {
        throw std::runtime_error("unsupported compression method in " + entry.name + " (use Deflate or Store)");
    }
    if (crc32(data.data(), data.size()) != entry.crc) throw std::runtime_error("CRC mismatch in " + entry.name);
    return data;
}
}
