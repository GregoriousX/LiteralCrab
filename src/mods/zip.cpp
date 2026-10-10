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
    void put16(std::string& out, std::uint16_t value) {
        out += static_cast<char>(value & 0xFF);
        out += static_cast<char>(value >> 8);
    }
    void put32(std::string& out, std::uint32_t value) {
        put16(out, static_cast<std::uint16_t>(value & 0xFFFF));
        put16(out, static_cast<std::uint16_t>(value >> 16));
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

    class Output {
    public:
        explicit Output(const std::wstring& path) : handle(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)) {
            if (handle == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot create the cache file");
            buffer.reserve(chunk);
        }
        Output(const Output&) = delete;
        Output& operator=(const Output&) = delete;
        ~Output() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        void write(const void* data, std::size_t size) {
            const auto* bytes = static_cast<const char*>(data);
            if (buffer.size() + size > chunk) flush();
            if (size >= chunk) return direct(bytes, size);
            buffer.insert(buffer.end(), bytes, bytes + size);
            written += size;
        }
        void close() {
            flush();
            CloseHandle(handle);
            handle = INVALID_HANDLE_VALUE;
        }
        std::uint64_t position() const noexcept { return written; }
    private:
        void direct(const char* data, std::size_t size) {
            DWORD done{};
            if (!WriteFile(handle, data, static_cast<DWORD>(size), &done, nullptr) || done != size)
                throw std::runtime_error("cannot write the cache file");
            written += size;
        }
        void flush() {
            if (buffer.empty()) return;
            DWORD done{};
            if (!WriteFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &done, nullptr) || done != buffer.size())
                throw std::runtime_error("cannot write the cache file");
            buffer.clear();
        }
        HANDLE handle;
        std::vector<char> buffer;
        std::uint64_t written{};
    };
}

std::uint32_t crc32(const void* data, std::size_t size, std::uint32_t crc) noexcept {
    const auto* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (std::size_t index = 0; index < size; ++index) crc = crcTable.table[(crc ^ bytes[index]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

// RFC 1321. Self-contained: initialization runs under the loader lock, where loading a crypto
// provider DLL is not safe.
Md5 md5(const void* data, std::size_t size) {
    static constexpr std::uint32_t shifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
        5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 5, 9, 14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
        4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    static constexpr std::uint32_t constants[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    std::uint32_t state[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const auto block = [&](const unsigned char* chunkBytes) {
        std::uint32_t words[16];
        for (int index = 0; index < 16; ++index) words[index] = u32(chunkBytes + index * 4);
        std::uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        for (std::uint32_t round = 0; round < 64; ++round) {
            std::uint32_t f, g;
            if (round < 16) { f = (b & c) | (~b & d); g = round; }
            else if (round < 32) { f = (d & b) | (~d & c); g = (5 * round + 1) % 16; }
            else if (round < 48) { f = b ^ c ^ d; g = (3 * round + 5) % 16; }
            else { f = c ^ (b | ~d); g = (7 * round) % 16; }
            const std::uint32_t sum = a + f + constants[round] + words[g];
            a = d;
            d = c;
            c = b;
            b += (sum << shifts[round]) | (sum >> (32 - shifts[round]));
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
    };
    const auto* bytes = static_cast<const unsigned char*>(data);
    std::size_t offset = 0;
    for (; size - offset >= 64; offset += 64) block(bytes + offset);
    unsigned char tail[128]{};
    const std::size_t rest = size - offset;
    if (rest) std::memcpy(tail, bytes + offset, rest);
    tail[rest] = 0x80;
    const std::size_t padded = rest < 56 ? 64 : 128;
    const std::uint64_t bits = static_cast<std::uint64_t>(size) * 8;
    for (int index = 0; index < 8; ++index) tail[padded - 8 + index] = static_cast<unsigned char>(bits >> (8 * index));
    block(tail);
    if (padded == 128) block(tail + 64);
    Md5 digest{};
    for (int index = 0; index < 16; ++index) digest[index] = static_cast<std::uint8_t>(state[index / 4] >> (8 * (index % 4)));
    return digest;
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
            for (const auto* extra = cursor + 46 + nameLength, *stop = extra + extraLength; stop - extra >= 4;) {
                const auto tag = u16(extra), fieldLength = u16(extra + 2);
                if (stop - extra - 4 < fieldLength) break;
                if (tag == 0x464B && fieldLength >= 19 && !std::memcmp(extra + 4, "MD5", 3)) {
                    std::memcpy(entry.md5.data(), extra + 7, 16);
                    entry.hasMd5 = true;
                }
                extra += 4 + fieldLength;
            }
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

void Archive::copyRaw(const Entry& entry, const std::function<void(const void*, std::size_t)>& sink) const {
    auto offset = dataOffset(entry);
    std::string buffer((std::min<std::size_t>)(chunk, entry.compressedSize), '\0');
    for (std::size_t left = entry.compressedSize; left;) {
        const auto part = (std::min)(left, buffer.size());
        readAt(offset, buffer.data(), part);
        sink(buffer.data(), part);
        offset += part;
        left -= part;
    }
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

void writeGameArchive(const std::wstring& path, const std::vector<Member>& members,
    std::uint16_t time, std::uint16_t date) {
    if (members.size() > 0xFFFF) throw std::runtime_error("more than 65535 members");
    // Central directory first in memory: its size fixes every local header offset.
    std::uint64_t directorySize = 0;
    for (const auto& member : members) directorySize += 46 + member.name.size() + 23;
    const std::uint64_t front = 22 + directorySize;
    std::string directory;
    directory.reserve(static_cast<std::size_t>(directorySize));
    std::uint64_t offset = front;
    for (const auto& member : members) {
        if (offset > 0xFFFFFFFF) throw std::runtime_error("archive exceeds 4 GiB");
        put32(directory, centralSignature);
        put16(directory, 20);
        put16(directory, 20);
        put16(directory, 0);
        put16(directory, member.method);
        put16(directory, time);
        put16(directory, date);
        put32(directory, member.crc);
        put32(directory, member.compressedSize);
        put32(directory, member.size);
        put16(directory, static_cast<std::uint16_t>(member.name.size()));
        put16(directory, 23);
        put16(directory, 0);
        put16(directory, 0);
        put16(directory, 0);
        put32(directory, 0);
        put32(directory, static_cast<std::uint32_t>(offset));
        directory += member.name;
        put16(directory, 0x464B);
        put16(directory, 19);
        directory.append("MD5", 3);
        directory.append(reinterpret_cast<const char*>(member.md5.data()), member.md5.size());
        offset += 30 + member.name.size() + member.compressedSize;
    }
    if (offset > 0xFFFFFFFF) throw std::runtime_error("archive exceeds 4 GiB");
    std::string end;
    put32(end, endSignature);
    put16(end, 0);
    put16(end, 0);
    put16(end, static_cast<std::uint16_t>(members.size()));
    put16(end, static_cast<std::uint16_t>(members.size()));
    put32(end, static_cast<std::uint32_t>(directorySize));
    put32(end, static_cast<std::uint32_t>(offset));
    put16(end, 0);

    const auto temporary = path + L".tmp";
    try {
        Output output(temporary);
        output.write(end.data(), end.size());
        output.write(directory.data(), directory.size());
        for (const auto& member : members) {
            std::string header;
            put32(header, localSignature);
            put16(header, 20);
            put16(header, 0);
            put16(header, member.method);
            put16(header, time);
            put16(header, date);
            put32(header, member.crc);
            put32(header, member.compressedSize);
            put32(header, member.size);
            put16(header, static_cast<std::uint16_t>(member.name.size()));
            put16(header, 0);
            header += member.name;
            output.write(header.data(), header.size());
            const auto start = output.position();
            member.write([&](const void* data, std::size_t size) { output.write(data, size); });
            if (output.position() - start != member.compressedSize)
                throw std::runtime_error("member size changed while writing: " + member.name);
        }
        output.write(directory.data(), directory.size());
        output.write(end.data(), end.size());
        output.close();
    } catch (...) {
        DeleteFileW(temporary.c_str());
        throw;
    }
    if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        DeleteFileW(temporary.c_str());
        throw std::runtime_error("cannot replace the cache file");
    }
}
}
