// SPDX-License-Identifier: GPL-3.0-only
#include "di1/portrait_texture.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdint>
#include <stdexcept>
#include <string>

using crabe::di1::makePortalPortrait;
namespace {
unsigned failures{};
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL line %d: %s\n", __LINE__, #condition); ++failures; } } while (0)
void put(std::string& bytes, unsigned at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<char>(value >> (8 * i));
}
unsigned word(const std::string& bytes, unsigned at) {
    unsigned value{};
    for (unsigned i = 0; i < 4; ++i) value |= static_cast<unsigned char>(bytes[at + i]) << (8 * i);
    return value;
}
std::string rgba(unsigned width, unsigned height, unsigned pitch = 0) {
    if (!pitch) pitch = width * 4;
    std::string data(128 + static_cast<std::size_t>(pitch) * height, '\0');
    data.replace(0, 4, "DDS "); put(data, 4, 124); put(data, 8, 0x100F);
    put(data, 12, height); put(data, 16, width); put(data, 20, pitch);
    put(data, 76, 32); put(data, 80, 0x41); put(data, 88, 32);
    put(data, 92, 0xFF0000); put(data, 96, 0xFF00); put(data, 100, 0xFF);
    put(data, 104, 0xFF000000); put(data, 108, 0x1000);
    return data;
}
void pixel(std::string& bytes, unsigned x, unsigned y, std::array<unsigned, 4> bgra) {
    const auto at = 128 + static_cast<std::size_t>(y) * word(bytes, 20) + x * 4;
    for (unsigned i = 0; i < 4; ++i) bytes[at + i] = static_cast<char>(bgra[i]);
}
std::array<unsigned, 4> pixel(const std::string& bytes, unsigned x, unsigned y) {
    const auto at = 128 + static_cast<std::size_t>(y) * word(bytes, 20) + x * 4;
    return {static_cast<unsigned char>(bytes[at]), static_cast<unsigned char>(bytes[at + 1]),
        static_cast<unsigned char>(bytes[at + 2]), static_cast<unsigned char>(bytes[at + 3])};
}
std::array<unsigned, 4> bounds(const std::string& bytes) {
    std::array<unsigned, 4> found{word(bytes, 16), word(bytes, 12), 0, 0};
    for (unsigned y = 0; y < word(bytes, 12); ++y)
        for (unsigned x = 0; x < word(bytes, 16); ++x)
            if (pixel(bytes, x, y)[3]) {
                found[0] = (std::min)(found[0], x); found[1] = (std::min)(found[1], y);
                found[2] = (std::max)(found[2], x); found[3] = (std::max)(found[3], y);
            }
    return found;
}
void reject(const std::string& bytes, unsigned size = 512) {
    bool threw{};
    try { (void)makePortalPortrait(bytes, size); } catch (const std::runtime_error&) { threw = true; }
    CHECK(threw);
}
std::string block(const char* format, unsigned width = 4, unsigned height = 4) {
    auto bytes = rgba(width, height);
    bytes.resize(128 + (std::string(format) == "DXT1" ? 8 : 16), '\0');
    std::fill(bytes.begin() + 128, bytes.end(), '\0');
    put(bytes, 8, 0x81007); put(bytes, 20, static_cast<unsigned>(bytes.size() - 128));
    put(bytes, 80, 4); bytes.replace(84, 4, format);
    return bytes;
}
void headersAndValidation() {
    auto source = rgba(3, 2, 16); pixel(source, 2, 1, {7, 23, 199, 255});
    // Padding bytes must not turn into visible source texels.
    std::fill(source.begin() + 140, source.begin() + 144, static_cast<char>(255));
    const auto result = makePortalPortrait(source, 256);
    CHECK(result.size() == 128 + 256 * 256 * 4);
    CHECK(result.substr(0, 4) == "DDS " && word(result, 4) == 124 && word(result, 76) == 32);
    CHECK(word(result, 12) == 256 && word(result, 16) == 256 && word(result, 20) == 1024);
    CHECK(word(result, 80) == 0x41 && word(result, 88) == 32 && word(result, 108) == 0x1000);
    CHECK(pixel(result, 128, 200) == (std::array<unsigned, 4>{7, 23, 199, 255}));
    CHECK(pixel(result, 0, 0) == (std::array<unsigned, 4>{0, 0, 0, 0}));
    CHECK(bounds(result) == (std::array<unsigned, 4>{38, 70, 217, 249}));
    // Existing lower mip levels are deliberately ignored.
    source.append(100, static_cast<char>(255)); put(source, 28, 4);
    CHECK(makePortalPortrait(source, 256) == result);
    reject(""); reject(source.substr(0, 127));
    auto bad = source; put(bad, 4, 123); reject(bad);
    bad = source; put(bad, 76, 31); reject(bad);
    bad = source; put(bad, 8, 0x1001); reject(bad);
    bad = source; put(bad, 16, 0); reject(bad);
    bad = source; put(bad, 16, 8193); reject(bad);
    bad = source; put(bad, 16, 8192); put(bad, 12, 8192); reject(bad);
    bad = source; put(bad, 20, 11); reject(bad);
    bad = source; put(bad, 20, 0xFFFFFFFF); reject(bad);
    bad = source; put(bad, 92, 0xFF); reject(bad);
    bad = source; put(bad, 112, 0x200); reject(bad);
    bad = source; put(bad, 24, 2); reject(bad);
    bad = rgba(3, 2, 16); bad.pop_back(); reject(bad);
    reject(source, 0); reject(source, 4097);
    const auto empty = makePortalPortrait(rgba(2, 2));
    CHECK(std::all_of(empty.begin() + 128, empty.end(), [](char byte) { return byte == 0; }));
}
void framingAndAlpha() {
    auto tall = rgba(8, 12);
    for (unsigned y = 2; y < 10; ++y) for (unsigned x = 2; x < 6; ++x) pixel(tall, x, y, {0, 0, 255, 255});
    CHECK(bounds(makePortalPortrait(tall)) == (std::array<unsigned, 4>{163, 130, 348, 499}));
    CHECK(bounds(makePortalPortrait(tall, 512, true)) == (std::array<unsigned, 4>{157, 116, 354, 511}));
    CHECK(bounds(makePortalPortrait(tall, 256)) == (std::array<unsigned, 4>{81, 65, 174, 249}));
    auto wide = rgba(8, 2);
    for (unsigned y = 0; y < 2; ++y) for (unsigned x = 0; x < 8; ++x) pixel(wide, x, y, {0, 255, 0, 255});
    CHECK(bounds(makePortalPortrait(wide)) == (std::array<unsigned, 4>{76, 410, 435, 499}));
    CHECK(bounds(makePortalPortrait(wide, 512, true)) == (std::array<unsigned, 4>{64, 416, 447, 511}));
    // Opaque red beside transparent blue: interpolation may change alpha, never
    // tint red purple/black. A second red texel keeps the transparent gap in bounds.
    auto halo = rgba(3, 1); pixel(halo, 0, 0, {0, 0, 255, 255});
    pixel(halo, 1, 0, {255, 0, 0, 0}); pixel(halo, 2, 0, {0, 0, 255, 255});
    const auto result = makePortalPortrait(halo);
    bool fractional{};
    for (unsigned y = 0; y < 512; ++y) for (unsigned x = 0; x < 512; ++x) {
        const auto p = pixel(result, x, y);
        if (p[3]) { CHECK(p[0] == 0 && p[1] == 0 && p[2] == 255); fractional |= p[3] < 255; }
        else CHECK(p[0] == 0 && p[1] == 0 && p[2] == 0);
    }
    CHECK(fractional);
    auto faint = rgba(1, 1); pixel(faint, 0, 0, {12, 34, 56, 1});
    CHECK(pixel(makePortalPortrait(faint), 256, 300) == (std::array<unsigned, 4>{12, 34, 56, 1}));
}
void compressedFormats() {
    auto dxt1 = block("DXT1"); dxt1[128] = static_cast<char>(0xE0); dxt1[129] = 7; // Green 565.
    CHECK(pixel(makePortalPortrait(dxt1), 256, 300) == (std::array<unsigned, 4>{0, 255, 0, 255}));
    // DXT1 c0<=c1, code3 is transparent, even if color endpoints are nonzero.
    dxt1[128] = 0; dxt1[129] = 0; dxt1[130] = static_cast<char>(255); dxt1[131] = static_cast<char>(255);
    std::fill(dxt1.begin() + 132, dxt1.end(), static_cast<char>(255));
    const auto transparent = makePortalPortrait(dxt1);
    CHECK(std::all_of(transparent.begin() + 128, transparent.end(), [](char byte) { return byte == 0; }));
    std::fill(dxt1.begin() + 132, dxt1.end(), static_cast<char>(0xAA)); // code2: black/white midpoint.
    CHECK(pixel(makePortalPortrait(dxt1), 256, 300) == (std::array<unsigned, 4>{127, 127, 127, 255}));
    auto dxt3 = block("DXT3"); std::fill(dxt3.begin() + 128, dxt3.begin() + 136, static_cast<char>(0x88));
    dxt3[136] = 0; dxt3[137] = static_cast<char>(0xF8); // Red, alpha8/15.
    CHECK(pixel(makePortalPortrait(dxt3), 256, 300) == (std::array<unsigned, 4>{0, 0, 255, 136}));
    // DXT3/5 always use four-color interpolation even when c0<=c1.
    dxt3[136] = 0; dxt3[137] = 0; dxt3[138] = static_cast<char>(255); dxt3[139] = static_cast<char>(255);
    std::fill(dxt3.begin() + 140, dxt3.end(), static_cast<char>(255));
    CHECK(pixel(makePortalPortrait(dxt3), 256, 300) == (std::array<unsigned, 4>{170, 170, 170, 136}));
    auto dxt5 = block("DXT5"); dxt5[128] = static_cast<char>(200); dxt5[129] = 60;
    // Every alpha code=2 -> (6*200+60)/7=180.
    std::uint64_t codes{}; for (unsigned i = 0; i < 16; ++i) codes |= std::uint64_t(2) << (i * 3);
    for (unsigned i = 0; i < 6; ++i) dxt5[130 + i] = static_cast<char>(codes >> (i * 8));
    dxt5[136] = 31; // Blue.
    CHECK(pixel(makePortalPortrait(dxt5), 256, 300) == (std::array<unsigned, 4>{255, 0, 0, 180}));
    dxt5[128] = 0; dxt5[129] = static_cast<char>(100);
    codes = 0; for (unsigned i = 0; i < 16; ++i) codes |= std::uint64_t(7) << (i * 3);
    for (unsigned i = 0; i < 6; ++i) dxt5[130 + i] = static_cast<char>(codes >> (i * 8));
    CHECK(pixel(makePortalPortrait(dxt5), 256, 300)[3] == 255);
    codes = 0; for (unsigned i = 0; i < 16; ++i) codes |= std::uint64_t(2) << (i * 3);
    for (unsigned i = 0; i < 6; ++i) dxt5[130 + i] = static_cast<char>(codes >> (i * 8));
    CHECK(pixel(makePortalPortrait(dxt5), 256, 300)[3] == 20);
    codes = 0; for (unsigned i = 0; i < 16; ++i) codes |= std::uint64_t(6) << (i * 3);
    for (unsigned i = 0; i < 6; ++i) dxt5[130 + i] = static_cast<char>(codes >> (i * 8));
    const auto transparentDxt5 = makePortalPortrait(dxt5);
    CHECK(std::all_of(transparentDxt5.begin() + 128, transparentDxt5.end(), [](char byte) { return byte == 0; }));
    // Partial edge blocks are clipped to actual image dimensions, not decoded padding.
    auto partial = block("DXT1", 3, 2); partial[128] = static_cast<char>(0xE0); partial[129] = 7;
    CHECK(bounds(makePortalPortrait(partial)) == (std::array<unsigned, 4>{76, 260, 435, 499}));
    for (const auto* format : {"DXT1", "DXT3", "DXT5"}) {
        auto bad = block(format); bad.pop_back(); reject(bad);
        bad = block(format); put(bad, 20, 1); reject(bad);
    }
    auto bad = block("DXT1"); bad.replace(84, 4, "DX10"); reject(bad);
    bad = block("DXT1"); put(bad, 8, 0x8100F); reject(bad);
    bad = block("DXT1"); put(bad, 80, 0x44); reject(bad);
}
}
int main() {
    headersAndValidation(); framingAndAlpha(); compressedFormats();
    if (failures) return 1;
    std::puts("PASS: portal DDS decoding, alpha-safe framing, DDS validation and DXT1/3/5");
    return 0;
}
