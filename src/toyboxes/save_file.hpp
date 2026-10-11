// SPDX-License-Identifier: GPL-3.0-only
// Disney Infinity 1.0 save files, as the PC game and the consoles write them.
//
// A file is a 0x40-byte header {0x207, file size, 1, text size, payload size} followed by the
// payload: a CMP1 block {"CMP1", text size, zlib size, lookup2(text), lookup2(zlib)} and the zlib
// stream, padded to 0x40 bytes. PC files are little-endian and plain. PS3 and Wii U files are
// big-endian and their payload is encrypted with the game's Phelix variant (the key is a string
// in the executable, the nonce is zero). The text inside is identical on every platform.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace crabe::toyboxes {
// Bob Jenkins' lookup2, the game's CMP1 checksum (0x40B870 in the PC executable).
std::uint32_t lookup2(const void* data, std::size_t size, std::uint32_t initial = 0) noexcept;
// Decrypts whole 32-bit words in place; a trailing partial word is left as it is (the game pads
// payloads to 0x40 bytes). bigEndian selects the console word order.
void phelixDecrypt(unsigned char* data, std::size_t size, bool bigEndian) noexcept;

// A verified CMP1 block with little-endian fields, without padding.
struct Cmp1 {
    std::string block;
    std::uint32_t textSize() const noexcept;
};
// Accepts a PC or console save file and returns its CMP1 block after checking the container
// and both checksums. Throws std::runtime_error with the reason.
Cmp1 decodeFile(std::string_view file);
// The text a block holds; verifies its checksum. Throws std::runtime_error.
std::string inflate(const Cmp1& cmp1);
// A block for the text. The zlib stream uses stored blocks: the game inflates it like any other.
Cmp1 encode(std::string_view text);
// A PC save file holding the block.
std::string pcFile(const Cmp1& cmp1);

enum class Kind { Unknown, UgcData, UgcHeader, UgcScreen, SlotData, SlotHeader, SlotScreen };
Kind classify(std::string_view text) noexcept;

// Value of "\t<key> = ..." in a header: the text between quotes for strings, the rest of the
// line otherwise. Empty when absent.
std::string field(std::string_view text, std::string_view key);
// The world a Toy Box data file belongs to (CurrentEnv), or empty.
std::string world(std::string_view data);
// True for the worlds players build and play Toy Boxes in: the empty and default Toy Boxes, the
// play set themed ones and the prebuilt Toy Box worlds. Hubs with progress of their own (Toy Box
// Launch, Hall of Heroes, the introduction flow), menus and development worlds are not.
bool toyBoxWorld(std::string_view world) noexcept;

// Shared Toy Box conversions, all producing the UGC (Toy Box Share) layout the menu lists.
// A play set save's data: its zone's RumpusSaveGame and MeridianGraphs blocks.
std::string ugcDataFromSlot(std::string_view slotData);
// The Toy Box Share tab a header lists its Toy Box in (its TARGET): "public" is Disney's Toy
// Boxes, "private" is My Toy Boxes.
constexpr std::string_view publicTarget = "public", privateTarget = "private";
// A header for data, named name. Throws when the data has no Toy Box.
std::string ugcHeader(std::string_view data, std::string_view name, std::string_view description,
    std::string_view time, std::string_view date, std::string_view timestamp, std::string_view level,
    std::string_view target);
// The UGC header with its TARGET set to target.
std::string withTarget(std::string_view ugcHeader, std::string_view target);
// A play set screenshot as a UGC screenshot.
std::string ugcScreen(std::string_view slotScreen);

// Checks for Toy Boxes from untrusted packages, so the game only parses what it wrote itself.
// Toy Box data: version 31, a sane object count and a CreateId list at least that long. Throws.
void validateData(std::string_view data);
// A screenshot whose declared size and dimensions match its DXT1 pixel data.
bool validScreen(std::string_view screen) noexcept;
}
