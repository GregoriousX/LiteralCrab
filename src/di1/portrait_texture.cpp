// SPDX-License-Identifier: GPL-3.0-only
#include "di1/portrait_texture.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace crabe::di1 {
namespace {
constexpr std::uint32_t pitchFlag = 0x8, linearSizeFlag = 0x80000;
constexpr std::uint32_t rgbFlag = 0x40, alphaPixelsFlag = 0x1, fourCcFlag = 0x4;
constexpr std::size_t maximumPixels = 16 * 1024 * 1024;
struct Pixel { std::uint8_t b{}, g{}, r{}, a{}; };
struct Image { unsigned width{}, height{}; std::vector<Pixel> pixels; };

[[noreturn]] void invalid(const char* message) { throw std::runtime_error(message); }
std::uint32_t word(std::string_view bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 4) invalid("truncated DDS header");
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i)
        value |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[offset + i])) << (8 * i);
    return value;
}
void put(std::string& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = static_cast<char>(value >> (8 * i));
}
std::uint16_t half(const unsigned char* bytes) {
    return static_cast<std::uint16_t>(bytes[0] | (static_cast<unsigned>(bytes[1]) << 8));
}
Pixel color565(std::uint16_t color) {
    const auto b = static_cast<unsigned>(color & 31);
    const auto g = static_cast<unsigned>((color >> 5) & 63);
    const auto r = static_cast<unsigned>((color >> 11) & 31);
    return {static_cast<std::uint8_t>((b << 3) | (b >> 2)),
        static_cast<std::uint8_t>((g << 2) | (g >> 4)),
        static_cast<std::uint8_t>((r << 3) | (r >> 2)), 255};
}
Pixel mix(Pixel first, Pixel second, unsigned firstWeight, unsigned secondWeight, unsigned divisor) {
    return {static_cast<std::uint8_t>((first.b * firstWeight + second.b * secondWeight) / divisor),
        static_cast<std::uint8_t>((first.g * firstWeight + second.g * secondWeight) / divisor),
        static_cast<std::uint8_t>((first.r * firstWeight + second.r * secondWeight) / divisor), 255};
}
std::array<Pixel, 16> decodeBlock(const unsigned char* bytes, unsigned format) {
    const auto* colors = bytes + (format == 1 ? 0 : 8);
    const auto first = half(colors), second = half(colors + 2);
    std::array<Pixel, 4> palette{color565(first), color565(second), {}, {}};
    if (format != 1 || first > second) {
        palette[2] = mix(palette[0], palette[1], 2, 1, 3);
        palette[3] = mix(palette[0], palette[1], 1, 2, 3);
    } else {
        palette[2] = mix(palette[0], palette[1], 1, 1, 2);
        palette[3] = {}; // DXT1's transparent code; RGB must not contribute on resampling.
    }
    std::uint32_t codes{};
    for (unsigned i = 0; i < 4; ++i) codes |= static_cast<std::uint32_t>(colors[4 + i]) << (8 * i);
    std::array<Pixel, 16> result{};
    for (unsigned i = 0; i < 16; ++i) result[i] = palette[(codes >> (2 * i)) & 3];
    if (format == 3) {
        for (unsigned i = 0; i < 16; ++i)
            result[i].a = static_cast<std::uint8_t>(((bytes[i / 2] >> (4 * (i % 2))) & 15) * 17);
    } else if (format == 5) {
        std::array<unsigned, 8> alpha{bytes[0], bytes[1], 0, 0, 0, 0, 0, 0};
        if (alpha[0] > alpha[1]) {
            for (unsigned i = 1; i <= 6; ++i) alpha[i + 1] = ((7 - i) * alpha[0] + i * alpha[1]) / 7;
        } else {
            for (unsigned i = 1; i <= 4; ++i) alpha[i + 1] = ((5 - i) * alpha[0] + i * alpha[1]) / 5;
            alpha[6] = 0; alpha[7] = 255;
        }
        std::uint64_t indices{};
        for (unsigned i = 0; i < 6; ++i) indices |= static_cast<std::uint64_t>(bytes[2 + i]) << (8 * i);
        for (unsigned i = 0; i < 16; ++i)
            result[i].a = static_cast<std::uint8_t>(alpha[static_cast<unsigned>((indices >> (3 * i)) & 7)]);
    }
    return result;
}
Image decode(std::string_view bytes) {
    if (bytes.size() < 128 || bytes.substr(0, 4) != "DDS " || word(bytes, 4) != 124 || word(bytes, 76) != 32)
        invalid("invalid legacy DDS header");
    const auto flags = word(bytes, 8), pixelFlags = word(bytes, 80);
    if ((flags & 0x1007) != 0x1007) invalid("DDS lacks required header flags");
    const auto height = word(bytes, 12), width = word(bytes, 16);
    if (!width || !height || width > 8192 || height > 8192 ||
        static_cast<std::size_t>(width) * height > maximumPixels) invalid("unsupported DDS dimensions");
    if (word(bytes, 24) > 1 || (word(bytes, 112) & 0x20FE00)) invalid("DDS volume/cubemap textures are unsupported");
    unsigned format{}, blockBytes{};
    std::size_t rowBytes{}, required{};
    if (pixelFlags & fourCcFlag) {
        if (pixelFlags & rgbFlag) invalid("ambiguous DDS pixel format");
        const auto fourCc = bytes.substr(84, 4);
        if (fourCc == "DXT1") { format = 1; blockBytes = 8; }
        else if (fourCc == "DXT3") { format = 3; blockBytes = 16; }
        else if (fourCc == "DXT5") { format = 5; blockBytes = 16; }
        else invalid("unsupported DDS compressed format");
        rowBytes = static_cast<std::size_t>((width + 3) / 4) * blockBytes;
        required = rowBytes * ((height + 3) / 4);
        if (flags & pitchFlag) invalid("compressed DDS must not declare an uncompressed row pitch");
        if ((flags & linearSizeFlag) && word(bytes, 20) != required) invalid("invalid DDS compressed linear size");
    } else {
        if ((pixelFlags & (rgbFlag | alphaPixelsFlag)) != (rgbFlag | alphaPixelsFlag) ||
            word(bytes, 88) != 32 || word(bytes, 92) != 0x00FF0000 || word(bytes, 96) != 0x0000FF00 ||
            word(bytes, 100) != 0x000000FF || word(bytes, 104) != 0xFF000000)
            invalid("unsupported DDS format; expected A8R8G8B8");
        rowBytes = static_cast<std::size_t>(width) * 4;
        if (flags & pitchFlag) {
            const auto pitch = word(bytes, 20);
            if (pitch < rowBytes) invalid("DDS row pitch is too small");
            rowBytes = pitch;
        }
        if (rowBytes > (std::numeric_limits<std::size_t>::max)() / height) invalid("DDS row pitch overflows");
        required = rowBytes * height;
    }
    if (required > bytes.size() - 128) invalid("truncated DDS top-level pixels");
    Image image{width, height, std::vector<Pixel>(static_cast<std::size_t>(width) * height)};
    const auto* data = reinterpret_cast<const unsigned char*>(bytes.data() + 128);
    if (!format) {
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const auto* pixel = data + static_cast<std::size_t>(y) * rowBytes + x * 4;
                image.pixels[static_cast<std::size_t>(y) * width + x] = {pixel[0], pixel[1], pixel[2], pixel[3]};
            }
    } else {
        for (unsigned by = 0; by < (height + 3) / 4; ++by)
            for (unsigned bx = 0; bx < (width + 3) / 4; ++bx) {
                const auto block = decodeBlock(data + static_cast<std::size_t>(by) * rowBytes + bx * blockBytes, format);
                for (unsigned y = 0; y < 4 && by * 4 + y < height; ++y)
                    for (unsigned x = 0; x < 4 && bx * 4 + x < width; ++x)
                        image.pixels[static_cast<std::size_t>(by * 4 + y) * width + bx * 4 + x] = block[y * 4 + x];
            }
    }
    return image;
}
unsigned rounded(double value) { return static_cast<unsigned>(std::clamp(std::lround(value), 0L, 255L)); }
}

std::string makePortalPortrait(std::string_view dds, unsigned size, bool selected) {
    if (!size || size > 4096) invalid("portal portrait size must be 1..4096");
    const auto image = decode(dds);
    std::string result(128 + static_cast<std::size_t>(size) * size * 4, '\0');
    result.replace(0, 4, "DDS "); put(result, 4, 124); put(result, 8, 0x100F);
    put(result, 12, size); put(result, 16, size); put(result, 20, size * 4); put(result, 28, 1);
    put(result, 76, 32); put(result, 80, rgbFlag | alphaPixelsFlag); put(result, 88, 32);
    put(result, 92, 0x00FF0000); put(result, 96, 0x0000FF00); put(result, 100, 0x000000FF);
    put(result, 104, 0xFF000000); put(result, 108, 0x1000);
    unsigned minX = image.width, minY = image.height, maxX{}, maxY{};
    bool visible{};
    for (unsigned y = 0; y < image.height; ++y)
        for (unsigned x = 0; x < image.width; ++x)
            if (image.pixels[static_cast<std::size_t>(y) * image.width + x].a) {
                minX = (std::min)(minX, x); minY = (std::min)(minY, y);
                maxX = (std::max)(maxX, x); maxY = (std::max)(maxY, y); visible = true;
            }
    if (!visible) return result;
    const auto canvasScale = static_cast<double>(size) / 512;
    const auto maxWidth = (selected ? 384.0 : 360.0) * canvasScale;
    const auto maxHeight = (selected ? 396.0 : 370.0) * canvasScale;
    const auto sourceWidth = static_cast<double>(maxX - minX + 1), sourceHeight = static_cast<double>(maxY - minY + 1);
    const auto scale = (std::min)(maxWidth / sourceWidth, maxHeight / sourceHeight);
    const auto width = sourceWidth * scale, height = sourceHeight * scale;
    const auto left = (size - width) / 2, right = left + width;
    const auto bottom = (selected ? 512.0 : 500.0) * canvasScale, top = bottom - height;
    // Coverage handles fractional destination edges. Sampling clamps to the source
    // bounds, while alpha-weighted interpolation excludes RGB in transparent pixels.
    const auto firstX = static_cast<unsigned>((std::max)(0.0, std::floor(left)));
    const auto firstY = static_cast<unsigned>((std::max)(0.0, std::floor(top)));
    const auto endX = static_cast<unsigned>((std::min)(static_cast<double>(size), std::ceil(right)));
    const auto endY = static_cast<unsigned>((std::min)(static_cast<double>(size), std::ceil(bottom)));
    for (unsigned y = firstY; y < endY; ++y) {
        const auto coverageY = (std::min)(y + 1.0, bottom) - (std::max)(static_cast<double>(y), top);
        const auto sy = std::clamp((y + 0.5 - top) / scale - 0.5 + minY, static_cast<double>(minY), static_cast<double>(maxY));
        const auto y0 = static_cast<unsigned>(sy), y1 = (std::min)(y0 + 1, maxY);
        const auto fy = sy - y0;
        for (unsigned x = firstX; x < endX; ++x) {
            const auto coverageX = (std::min)(x + 1.0, right) - (std::max)(static_cast<double>(x), left);
            const auto sx = std::clamp((x + 0.5 - left) / scale - 0.5 + minX, static_cast<double>(minX), static_cast<double>(maxX));
            const auto x0 = static_cast<unsigned>(sx), x1 = (std::min)(x0 + 1, maxX);
            const auto fx = sx - x0;
            const std::array<Pixel, 4> samples{image.pixels[static_cast<std::size_t>(y0) * image.width + x0],
                image.pixels[static_cast<std::size_t>(y0) * image.width + x1],
                image.pixels[static_cast<std::size_t>(y1) * image.width + x0],
                image.pixels[static_cast<std::size_t>(y1) * image.width + x1]};
            const std::array<double, 4> weights{(1 - fx) * (1 - fy), fx * (1 - fy), (1 - fx) * fy, fx * fy};
            double alpha{}, blue{}, green{}, red{};
            for (unsigned i = 0; i < samples.size(); ++i) {
                const auto weight = weights[i] * samples[i].a;
                alpha += weight; blue += weight * samples[i].b;
                green += weight * samples[i].g; red += weight * samples[i].r;
            }
            const auto outAlpha = rounded(alpha * coverageX * coverageY);
            if (!outAlpha || alpha <= 0) continue;
            const auto at = 128 + (static_cast<std::size_t>(y) * size + x) * 4;
            result[at] = static_cast<char>(rounded(blue / alpha));
            result[at + 1] = static_cast<char>(rounded(green / alpha));
            result[at + 2] = static_cast<char>(rounded(red / alpha));
            result[at + 3] = static_cast<char>(outAlpha);
        }
    }
    return result;
}
}
