// SPDX-License-Identifier: GPL-3.0-only
// [Startup] SkipIntro: start without the logo movies.
//
// At startup the game walks a list of eight logo slots (0xEDC5D0, called once by the startup
// sequence): Japanese legal and Namco screens, AvLogo, LOGO_HI_Wiu and DIS from assets\fmv.
// Each slot goes through a switch; a slot whose movie exists is queued and played before the
// next one, and two slots already jump straight to the loop's next iteration. The Japanese
// movies are not shipped on PC, so they are skipped the same way. The option points every slot
// of that switch at "next iteration", so no logo is queued and the startup sequence carries
// on as it does when the movies are absent. The movie files are neither replaced nor hidden,
// and other movies (attract loop, cinematics) are unaffected.
#include "di1/intro.hpp"
#include <cstdint>

namespace crabe::di1 {
namespace {
    constexpr std::uint32_t logosRva = 0xADC5D0, switchRva = 0xADC61E, playRva = 0xADC675, tableRva = 0xADC6E4;
    constexpr std::uint32_t nextSlotRva = 0xADC6A2, startupCallRva = 0xB1F409;
    // The switch targets in the supported build: legal, Namco, Namco alone, next, AvLogo,
    // LOGO_HI_Wiu, DIS, next.
    constexpr std::uint32_t targets[8] = {0xADC631, 0xADC640, 0xADC64A, nextSlotRva, 0xADC654, 0xADC65E, 0xADC668, nextSlotRva};
}

bool installIntroSkip(HMODULE host, const Config& options) {
    if (!options.skipIntro) return true;
    const auto base = reinterpret_cast<std::uintptr_t>(host);
    auto* table = reinterpret_cast<std::uintptr_t*>(base + tableRva);
    bool matches = codeMatches(base + logosRva, "6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50 64 89 25 00 00 00 00 83 EC 24 A0 ?? ?? ?? ?? 53 56") &&
        codeMatches(base + switchRva, "33 F6 0F 57 C0 33 C0 83 FE 07 77 4F FF 24 B5") &&
        *reinterpret_cast<const std::uintptr_t*>(base + switchRva + 15) == base + tableRva &&
        codeMatches(base + playRva, "88 5C 24 0A 53 51 F3 0F 11 04 24 6A 01 50 8D 4C 24 1C E8") &&
        callsTo(base, startupCallRva, logosRva) && readable(table, sizeof(targets));
    for (std::size_t slot = 0; matches && slot < 8; ++slot) matches = table[slot] == base + targets[slot];
    if (!matches) {
        log("Startup: SkipIntro unavailable: logo sequence differs; movies play as usual.");
        return false;
    }
    DWORD old{};
    if (!VirtualProtect(table, sizeof(targets), PAGE_READWRITE, &old)) {
        log("Startup: SkipIntro unavailable: the logo list cannot be changed.");
        return false;
    }
    for (std::size_t slot = 0; slot < 8; ++slot) table[slot] = base + nextSlotRva;
    VirtualProtect(table, sizeof(targets), old, &old);
    log("Startup: logo movies skipped.");
    return true;
}
}
