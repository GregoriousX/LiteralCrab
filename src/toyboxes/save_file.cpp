// SPDX-License-Identifier: GPL-3.0-only
#include "toyboxes/save_file.hpp"
#include "mods/zip.hpp"
#include <cstring>
#include <map>
#include <stdexcept>
#include <vector>

namespace crabe::toyboxes {
namespace {
    constexpr std::uint32_t version = 0x207, headerSize = 0x40, cmp1Size = 0x14;
    // Longest text the loader converts; the game's own Toy Boxes stay far below this.
    constexpr std::uint32_t maxText = 8u << 20;
    // More objects than the Toy Box editor can place; real Toy Boxes hold a few thousand.
    constexpr unsigned long maxObjects = 20000;

    std::uint32_t rotate(std::uint32_t value, int bits) noexcept { return value << bits | value >> (32 - bits); }
    std::uint32_t le32(const unsigned char* bytes) noexcept {
        return bytes[0] | bytes[1] << 8 | bytes[2] << 16 | static_cast<std::uint32_t>(bytes[3]) << 24;
    }
    std::uint32_t be32(const unsigned char* bytes) noexcept {
        return static_cast<std::uint32_t>(bytes[0]) << 24 | bytes[1] << 16 | bytes[2] << 8 | bytes[3];
    }
    void putLe32(std::string& out, std::uint32_t value) {
        for (int shift = 0; shift < 32; shift += 8) out.push_back(static_cast<char>(value >> shift));
    }

    // One word of the game's Phelix variant (0x40B240): the keystream is Z0 before the step.
    struct Phelix {
        std::uint32_t z[5]{}, x0[8]{}, x1[8]{}, block{};
        static void step(std::uint32_t (&z)[5], std::uint32_t key0, std::uint32_t text, std::uint32_t key1) noexcept {
            auto [z0, z1, z2, z3, z4] = z;
            z1 += z4; z0 += z3; z2 ^= z0; z3 = rotate(z3, 15) ^ z1;
            z4 = rotate(z4, 25) + z2; z0 = rotate(z0, 9) ^ (key0 + z3);
            z1 = rotate(z1, 10) ^ z4; z2 = rotate(z2, 17) + z0; z3 = rotate(z3, 30) + z1;
            z4 = rotate(z4, 13) ^ z2; z0 = rotate(z0, 20) + (z3 ^ text);
            z1 = rotate(z1, 11) + z4; z2 = rotate(z2, 5) ^ z0; z3 = rotate(z3, 15) ^ z1;
            z4 = rotate(z4, 25) + z2; z0 = rotate(z0, 9) ^ (key1 + z3);
            z1 = rotate(z1, 10) ^ z4; z2 = rotate(z2, 17) + z0;
            z[0] = rotate(z0, 20); z[1] = rotate(z1, 11); z[2] = rotate(z2, 5); z[3] = rotate(z3, 30) + z1; z[4] = rotate(z4, 13) ^ z2;
        }
        void next(std::uint32_t text) noexcept {
            const auto k = block & 7;
            step(z, x0[k], text, x1[k] + block);
            ++block;
        }
        explicit Phelix(bool bigEndian) noexcept {
            // 0x12807B0; the executable passes 31 bytes, so the key is zero-padded to 32.
            static constexpr char key[] = "IT aL1 stArt3d w1TH a m0uSe.:-)";
            constexpr std::uint32_t length = sizeof(key) - 1;
            unsigned char padded[32]{};
            std::memcpy(padded, key, length);
            std::uint32_t x[8];
            for (int word = 0; word < 8; ++word) x[word] = bigEndian ? be32(padded + 4 * word) : le32(padded + 4 * word);
            for (int round = 0; round < 8; ++round) {  // Key mixing (0x411030).
                const int k = 4 * (round & 1);
                std::uint32_t mixed[5] = {x[k], x[k + 1], x[k + 2], x[k + 3], length + 64};
                step(mixed, 0, 0, 0);
                for (int word = 0; word < 4; ++word) x[(k ^ 4) + word] ^= mixed[word];
            }
            // Nonce setup (0x411250) with the game's all-zero nonce.
            for (int word = 0; word < 8; ++word) x0[word] = x[word];
            for (std::uint32_t word = 0; word < 4; ++word) {
                x1[word] = x[word + 4];
                x1[word + 4] = x[word] + word;
            }
            x1[1] += length * 4;
            x1[5] += length * 4;
            z[0] = x[3]; z[1] = x[4]; z[2] = x[5]; z[3] = x[6]; z[4] = x[7];
            for (int round = 0; round < 8; ++round) next(0);
        }
    };

    std::string zlibStored(std::string_view data) {
        std::string out;
        out.reserve(data.size() + data.size() / 65535 * 5 + 16);
        out += "\x78\x01";
        std::size_t offset = 0;
        do {
            const auto length = static_cast<std::uint16_t>((std::min<std::size_t>)(data.size() - offset, 65535));
            const bool last = offset + length == data.size();
            out.push_back(last ? '\x01' : '\x00');
            out.push_back(static_cast<char>(length));
            out.push_back(static_cast<char>(length >> 8));
            out.push_back(static_cast<char>(~length));
            out.push_back(static_cast<char>(~length >> 8));
            out.append(data.substr(offset, length));
            offset += length;
        } while (offset < data.size());
        std::uint32_t a = 1, b = 0;
        for (const unsigned char byte : data) {
            a = (a + byte) % 65521;
            b = (b + a) % 65521;
        }
        const auto adler = b << 16 | a;
        for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(adler >> shift));
        return out;
    }

    bool startsWith(std::string_view text, std::string_view prefix) noexcept { return text.substr(0, prefix.size()) == prefix; }

    // Lines of a block written by the game: "<indent><key> =", "<indent>{", ..., "<indent>}".
    // Returns the block from its key line through its closing line, or empty when absent.
    std::string_view block(std::string_view text, std::size_t indent, std::string_view key) {
        std::string head(indent, '\t');
        head += key;
        head += " =\r\n";
        std::size_t start = 0;
        for (;;) {
            start = text.find(head, start);
            if (start == std::string_view::npos) return {};
            if (start == 0 || text[start - 1] == '\n') break;
            start += head.size();
        }
        const std::string close = "\r\n" + std::string(indent, '\t') + "}\r\n";
        const auto end = text.find(close, start + head.size());
        return end == std::string_view::npos ? std::string_view{} : text.substr(start, end + close.size() - start);
    }
    void appendDedented(std::string& out, std::string_view lines) {
        for (std::size_t start = 0; start < lines.size();) {
            auto end = lines.find('\n', start);
            end = end == std::string_view::npos ? lines.size() : end + 1;
            auto line = lines.substr(start, end - start);
            if (!line.empty() && line[0] == '\t') line.remove_prefix(1);
            out.append(line);
            start = end;
        }
    }
    std::string clean(std::string_view value, std::size_t limit) {
        std::string out;
        for (const char c : value)
            if (out.size() < limit && static_cast<unsigned char>(c) >= ' ' && c != '"' && c != '\\') out.push_back(c);
        return out;
    }
}

std::uint32_t lookup2(const void* data, std::size_t size, std::uint32_t initial) noexcept {
    const auto* k = static_cast<const unsigned char*>(data);
    std::uint32_t a = 0x9E3779B9, b = 0x9E3779B9, c = initial;
    const auto mix = [&] {
        a -= b; a -= c; a ^= c >> 13;
        b -= c; b -= a; b ^= a << 8;
        c -= a; c -= b; c ^= b >> 13;
        a -= b; a -= c; a ^= c >> 12;
        b -= c; b -= a; b ^= a << 16;
        c -= a; c -= b; c ^= b >> 5;
        a -= b; a -= c; a ^= c >> 3;
        b -= c; b -= a; b ^= a << 10;
        c -= a; c -= b; c ^= b >> 15;
    };
    std::size_t left = size;
    for (; left >= 12; left -= 12, k += 12) {
        a += le32(k);
        b += le32(k + 4);
        c += le32(k + 8);
        mix();
    }
    c += static_cast<std::uint32_t>(size);
    switch (left) {
    case 11: c += static_cast<std::uint32_t>(k[10]) << 24; [[fallthrough]];
    case 10: c += static_cast<std::uint32_t>(k[9]) << 16; [[fallthrough]];
    case 9: c += static_cast<std::uint32_t>(k[8]) << 8; [[fallthrough]];
    case 8: b += static_cast<std::uint32_t>(k[7]) << 24; [[fallthrough]];
    case 7: b += static_cast<std::uint32_t>(k[6]) << 16; [[fallthrough]];
    case 6: b += static_cast<std::uint32_t>(k[5]) << 8; [[fallthrough]];
    case 5: b += k[4]; [[fallthrough]];
    case 4: a += static_cast<std::uint32_t>(k[3]) << 24; [[fallthrough]];
    case 3: a += static_cast<std::uint32_t>(k[2]) << 16; [[fallthrough]];
    case 2: a += static_cast<std::uint32_t>(k[1]) << 8; [[fallthrough]];
    case 1: a += k[0]; break;
    default: break;
    }
    mix();
    return c;
}

void phelixDecrypt(unsigned char* data, std::size_t size, bool bigEndian) noexcept {
    Phelix cipher(bigEndian);
    for (std::size_t offset = 0; offset + 4 <= size; offset += 4) {
        auto* word = data + offset;
        const std::uint32_t text = (bigEndian ? be32(word) : le32(word)) ^ cipher.z[0];
        for (int byte = 0; byte < 4; ++byte)
            word[byte] = static_cast<unsigned char>(text >> (bigEndian ? 24 - 8 * byte : 8 * byte));
        cipher.next(text);
    }
}

std::uint32_t Cmp1::textSize() const noexcept {
    return block.size() >= cmp1Size ? le32(reinterpret_cast<const unsigned char*>(block.data()) + 4) : 0;
}

Cmp1 decodeFile(std::string_view file) {
    if (file.size() < headerSize + cmp1Size) throw std::runtime_error("too short for a save file");
    const auto* bytes = reinterpret_cast<const unsigned char*>(file.data());
    const bool pc = le32(bytes) == version, console = be32(bytes) == version;
    if (!pc && !console) throw std::runtime_error("not a Disney Infinity 1.0 save file");
    std::string payload(file.substr(headerSize));
    auto* data = reinterpret_cast<unsigned char*>(payload.data());
    if (console) phelixDecrypt(data, payload.size(), true);
    if (std::memcmp(data, "CMP1", 4)) throw std::runtime_error("no CMP1 block (unsupported or damaged save)");
    const auto read = [&](int field) { return console ? be32(data + 4 * field) : le32(data + 4 * field); };
    const std::uint32_t text = read(1), zlib = read(2), textHash = read(3), zlibHash = read(4);
    if (!text || text > maxText || zlib < 8 || zlib > payload.size() - cmp1Size) throw std::runtime_error("CMP1 sizes out of range");
    if (lookup2(data + cmp1Size, zlib) != zlibHash) throw std::runtime_error("CMP1 checksum mismatch (damaged save)");
    Cmp1 result;
    result.block = "CMP1";
    for (const auto value : {text, zlib, textHash, zlibHash}) putLe32(result.block, value);
    result.block.append(payload, cmp1Size, zlib);
    return result;
}

std::string inflate(const Cmp1& cmp1) {
    const auto* data = reinterpret_cast<const unsigned char*>(cmp1.block.data());
    if (cmp1.block.size() < cmp1Size + 6) throw std::runtime_error("CMP1 block too short");
    const auto text = le32(data + 4), hash = le32(data + 12);
    // zlib header: deflate, no preset dictionary.
    if ((data[cmp1Size] & 0x0F) != 8 || (data[cmp1Size] * 256 + data[cmp1Size + 1]) % 31 || (data[cmp1Size + 1] & 0x20))
        throw std::runtime_error("CMP1 stream is not zlib");
    auto out = mods::zip::inflate(std::string_view(cmp1.block).substr(cmp1Size + 2), text);
    if (lookup2(out.data(), out.size()) != hash) throw std::runtime_error("CMP1 text checksum mismatch");
    return out;
}

Cmp1 encode(std::string_view text) {
    const auto zlib = zlibStored(text);
    Cmp1 result;
    result.block = "CMP1";
    putLe32(result.block, static_cast<std::uint32_t>(text.size()));
    putLe32(result.block, static_cast<std::uint32_t>(zlib.size()));
    putLe32(result.block, lookup2(text.data(), text.size()));
    putLe32(result.block, lookup2(zlib.data(), zlib.size()));
    result.block += zlib;
    return result;
}

std::string pcFile(const Cmp1& cmp1) {
    const auto payload = static_cast<std::uint32_t>((cmp1.block.size() + 0x3F) & ~std::size_t{0x3F});
    std::string out;
    out.reserve(headerSize + payload);
    for (const auto value : {version, payload + headerSize, 1u, cmp1.textSize(), payload}) putLe32(out, value);
    out.resize(headerSize, '\0');
    out += cmp1.block;
    out.resize(headerSize + payload, '\0');
    return out;
}

Kind classify(std::string_view text) noexcept {
    const bool screen = text.find("\r\n\tSCREENSHOT = ") != std::string_view::npos;
    if (startsWith(text, "CSaveGameUGC =")) return screen ? Kind::UgcScreen : Kind::UgcHeader;
    if (startsWith(text, "CSaveGameSlot =")) return screen ? Kind::SlotScreen : Kind::SlotHeader;
    const bool toyBox = text.find("RumpusSaveGame =\r\n") != std::string_view::npos;
    if (toyBox && startsWith(text, "zone =")) return Kind::UgcData;
    if (toyBox && startsWith(text, "zoneglobal =")) return Kind::SlotData;
    return Kind::Unknown;
}

std::string field(std::string_view text, std::string_view key) {
    const std::string head = "\n\t" + std::string(key) + " = ";
    const auto start = text.find(head);
    if (start == std::string_view::npos) return {};
    auto value = text.substr(start + head.size());
    value = value.substr(0, value.find('\r'));
    if (value.size() >= 2 && value.front() == '"' && value.back() == '"') value = value.substr(1, value.size() - 2);
    return std::string(value);
}

std::string world(std::string_view data) {
    constexpr std::string_view head = "CurrentEnv = \"";
    const auto start = data.find(head);
    if (start == std::string_view::npos) return {};
    const auto end = data.find('"', start + head.size());
    return end == std::string_view::npos ? std::string{} : std::string(data.substr(start + head.size(), end - start - head.size()));
}

bool toyBoxWorld(std::string_view world) noexcept {
    static constexpr std::string_view worlds[] = {
        "rumpusroom_blank", "rumpusroom_simple",
        "rumpusroom_cars", "rumpusroom_incworld", "rumpusroom_lr", "rumpusroom_mu", "rumpusroom_pirates", "rumpusroom_toystory",
        "rumpusroom_bridgeh", "rumpusroom_bridges", "rumpusroom_canyons", "rumpusroom_caves", "rumpusroom_ctf_l",
        "rumpusroom_ctf_s", "rumpusroom_dragon", "rumpusroom_flat", "rumpusroom_fortress", "rumpusroom_glider",
        "rumpusroom_hills", "rumpusroom_islands", "rumpusroom_mayan", "rumpusroom_maze", "rumpusroom_pball",
        "rumpusroom_sportsarena",
    };
    for (const auto name : worlds) {
        if (name.size() != world.size()) continue;
        bool same = true;
        for (std::size_t at = 0; same && at < name.size(); ++at)
            same = name[at] == (world[at] >= 'A' && world[at] <= 'Z' ? world[at] + 32 : world[at]);
        if (same) return true;
    }
    return false;
}

std::string ugcDataFromSlot(std::string_view slot) {
    // zoneglobal = {...}, then zone = { _<world>_ = { rumpus = { RumpusSaveGame }, meridian = {
    // MeridianGraphs }, ... } }, then the play set's own state. UGC data keeps only the two blocks.
    constexpr std::string_view zoneHead = "\r\nzone =\r\n{\r\n";
    const auto zone = slot.find("\r\nzone =\r\n{\r\n\t_");
    if (zone == std::string_view::npos) throw std::runtime_error("save has no zone");
    const auto lineStart = zone + zoneHead.size();
    const auto worldLine = slot.substr(lineStart, slot.find("\r\n", lineStart) - lineStart);
    const auto body = slot.substr(zone);
    const auto rumpus = block(body, 2, "rumpus");
    const auto game = block(rumpus, 3, "RumpusSaveGame");
    if (game.empty()) throw std::runtime_error("save has no Toy Box");
    const auto graphs = block(block(body, 2, "meridian"), 3, "MeridianGraphs");
    std::string out = "zone =\r\n{\r\n";
    out.append(worldLine);
    out += "\r\n\t{\r\n";
    appendDedented(out, game);
    appendDedented(out, graphs);
    out += "\t}\r\n}\r\n";
    out.push_back('\0');
    return out;
}

// The catalogue ids of a Toy Box's objects (its CreateId array). Throws when absent.
static std::vector<long> createIds(std::string_view data) {
    const auto ids = data.find("\tCreateId =\r\n");
    const auto open = ids == std::string_view::npos ? ids : data.find('[', ids);
    const auto close = open == std::string_view::npos ? open : data.find(']', open);
    if (close == std::string_view::npos) throw std::runtime_error("data has no CreateId list");
    std::vector<long> out;
    for (std::size_t at = open + 1; at < close;) {
        while (at < close && (data[at] < '0' || data[at] > '9') && data[at] != '-') ++at;
        if (at >= close) break;
        char* end{};
        out.push_back(std::strtol(data.data() + at, &end, 10));
        at = static_cast<std::size_t>(end - data.data());
        if (out.size() > maxObjects) throw std::runtime_error("too many objects");
    }
    return out;
}

void validateData(std::string_view data) {
    if (data.size() > maxText) throw std::runtime_error("Toy Box data too large");
    if (data.find("\tRR_SAVE_VERSION = 31\r\n") == std::string_view::npos) throw std::runtime_error("unsupported Toy Box version");
    const auto count = field(data.substr(data.find("RumpusSaveGame =")), "\t\tNUM_CHILDREN");
    char* end{};
    const auto objects = std::strtoul(count.c_str(), &end, 10);
    if (count.empty() || *end || objects > maxObjects) throw std::runtime_error("object count out of range");
    // The game's own saves may list more objects than they count, never fewer: a shorter list
    // would make the game read past it.
    if (createIds(data).size() < objects) throw std::runtime_error("object list shorter than the object count");
}

bool validScreen(std::string_view screen) noexcept {
    try {
        const auto number = [&](std::string_view key) { return std::strtoul(field(screen, key).c_str(), nullptr, 10); };
        const auto size = number("SCREENSHOT_SIZE"), width = number("SCREENSHOT_WIDTH"), height = number("SCREENSHOT_HEIGHT");
        // Format 1 is DXT1: 8 bytes per 4x4 block. The game's screenshots are about 400x230.
        if (number("SCREENSHOT_FORMAT") != 1 || !width || !height || width > 1024 || height > 1024 || width % 4 ||
            height % 4 || size != width * height / 2) return false;
        const auto start = screen.find("\r\n\t$\r\n");
        const auto end = start == std::string_view::npos ? start : screen.find("\r\n\t$\r\n", start + 1);
        if (end == std::string_view::npos) return false;
        std::size_t digits = 0;
        for (const char c : screen.substr(start + 6, end - start - 6)) {
            if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')) ++digits;
            else if (c != '\t' && c != '\r' && c != '\n') return false;
        }
        return digits == 2ull * size;
    } catch (...) {
        return false;
    }
}

std::string ugcHeader(std::string_view data, std::string_view name, std::string_view description,
    std::string_view time, std::string_view date, std::string_view timestamp, std::string_view level,
    std::string_view target) {
    // OBJECTCOUNTS: how often each catalogue id occurs in the Toy Box (its CreateId array).
    std::map<long, unsigned> counts;
    for (const auto id : createIds(data))
        if (id >= 0) ++counts[id];
    std::string objects = "{";
    for (const auto& [id, count] : counts) {
        if (objects.size() > 1) objects += ',';
        objects += "\"\"" + std::to_string(id) + "\"\": " + std::to_string(count);
    }
    objects += '}';
    std::string out = "CSaveGameUGC =\r\n{\r\n\tCLOUDID = \"\"\r\n";
    out += "\tAUTHOREDNAME = \"" + clean(name, 63) + "\"\r\n";
    out += "\tAUTHOREDDESC = \"" + clean(description, 255) + "\"\r\n";
    out += "\tCREATOR = \"unknown\"\r\n";
    out += "\tTIMESTRING = \"" + clean(time, 31) + "\"\r\n";
    out += "\tDATESTRING = \"" + clean(date, 31) + "\"\r\n";
    out += "\tTIMESTAMP = " + std::to_string(std::strtoll(std::string(timestamp).c_str(), nullptr, 10)) + "\r\n";
    out += "\tLASTLEVEL = \"" + clean(level, 63) + "\"\r\n";
    out += "\tOBJECTCOUNTS = \"" + objects + "\"\r\n";
    out += "\tTARGET = \"" + std::string(target) + "\"\r\n}\r\n";
    out.push_back('\0');
    return out;
}

std::string withTarget(std::string_view header, std::string_view target) {
    std::string out(header);
    constexpr std::string_view head = "\r\n\tTARGET = \"";
    if (const auto at = out.find(head); at != std::string::npos) {
        const auto end = out.find('"', at + head.size());
        if (end != std::string::npos) out.replace(at + head.size(), end - at - head.size(), target);
    } else if (const auto last = out.rfind("\r\n}"); last != std::string::npos) {
        out.insert(last, std::string(head) + std::string(target) + "\"");
    }
    return out;
}

std::string ugcScreen(std::string_view screen) {
    constexpr std::string_view slot = "CSaveGameSlot";
    std::string out = "CSaveGameUGC";
    out.append(screen.substr(startsWith(screen, slot) ? slot.size() : 0));
    return out;
}
}
