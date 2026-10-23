// SPDX-License-Identifier: GPL-3.0-only
// AES primitives adapted from CrabeLoader's application/gateway.cpp (GPL-3.0).
// DI1's category key and row-dependent field IV are validated separately.
#include "di1/figure_registry.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <set>
#include <stdexcept>

namespace crabe::di1 {
namespace {
constexpr std::uint32_t firstCustomSku = 1000100, lastCustomSku = 1999999;
constexpr std::uint32_t stockRows = 38, maximumFigures = 64, maximumRows = stockRows + maximumFigures;
constexpr std::string_view stockNames[] = {
    "AV_Buzz", "AV_Woody", "AV_Jessie", "LR_LoneRanger", "LR_Tonto", "AV_McQueen", "AV_Mater",
    "AV_Cars_Francesco", "AV_Holly", "PIR_JackSparrow", "PIR_Barbossa", "PIR_DavyJones",
    "AV_MrIncredible", "AV_ElastiGirl", "AV_Dash", "AV_Violet", "AV_Syndrome", "MU_Sully", "MU_Mike",
    "MU_Randall", "TB_MickeyMouse", "NBC_JackSkellington", "TAN_Rapunzel", "FRO_Anna", "FRO_Elsa",
    "WR_Ralph", "WR_Vanellope", "PNF_Perry", "PNF_Phineas", "AV_MrIncredibleInfinite",
    "PIR_JackSparrowInfinite", "MU_SullyInfinite", "AV_McQueenInfinite", "LR_LoneRangerInfinite",
    "AV_BuzzInfinite", "PNF_PerryInfinite", "TB_MickeyMouseInfinite", "AV_Buzz_Glow"
};
// SourceAvatarData's portal Name keys are a separate namespace from gateway names.
// Both must remain reserved: the portal indexes its shipped rows by these strings.
constexpr std::string_view stockPortalNames[] = {
    "AV_MrInc", "AV_ElastiGirl", "Syndrome_name", "Violet_name", "Dash_name", "MU_Sully",
    "MU_Mike", "MU_Randall", "PIR_JackSparrow", "PIR_Barbossa", "PIR_DavyJones", "Woody_name",
    "Jessie_name", "Buzz_name", "WR_Ralph", "WR_Vanellope", "PNF_Phineas", "PNF_Perry",
    "mcqueen_name", "Mater_name", "holly_name", "Francesco_name", "LoneRanger_name", "Tonto_name",
    "FRO_Anna", "FRO_Elsa", "TAN_Rapunzel", "mickeymouse_name", "NBC_JackSkellington",
    "BobCrystal_name", "SulleyCrystal_name", "JackSparrowCrystal_name", "BuzzCrystal_name",
    "PerryCrystal_name", "McQueenCrystal_name", "LoneRangerCrystal_name", "MickeyMouseCrystal_name",
    "Buzz_Glow_name"
};

std::string lower(std::string_view value) {
    std::string result(value);
    for (auto& ch : result) if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
    return result;
}
bool stockName(std::string_view value) {
    const auto key = lower(value);
    return std::any_of(std::begin(stockNames), std::end(stockNames),
        [&](auto stock) { return key == lower(stock); });
}
bool reservedName(std::string_view value) {
    if (stockName(value)) return true;
    const auto key = lower(value);
    return std::any_of(std::begin(stockPortalNames), std::end(stockPortalNames),
        [&](auto stock) { return key == lower(stock); });
}
bool printable(std::string_view value, std::size_t maximum, bool empty = false) {
    return (empty || !value.empty()) && value.size() <= maximum &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) { return ch >= 32 && ch <= 126; });
}
bool identifier(std::string_view value) {
    return !value.empty() && value.size() <= 127 &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_';
        });
}
std::string ascii(std::wstring_view value) {
    std::string result;
    for (auto ch : value) {
        if (ch < 32 || ch > 126) throw std::runtime_error("Character fields must be printable ASCII");
        result.push_back(static_cast<char>(ch));
    }
    return result;
}
std::map<std::string, std::string> characterSection(const std::wstring& file, const wchar_t* section = L"Character") {
    std::array<wchar_t, 32768> buffer{};
    const auto count = GetPrivateProfileSectionW(section, buffer.data(),
        static_cast<DWORD>(buffer.size()), file.c_str());
    if (count >= buffer.size() - 2) throw std::runtime_error("Character section exceeds 32 KiB");
    std::map<std::string, std::string> fields;
    for (const wchar_t* entry = buffer.data(); *entry; entry += std::wcslen(entry) + 1) {
        std::wstring_view line(entry);
        if (line.front() == L';' || line.front() == L'#') continue;
        const auto split = line.find(L'=');
        if (split == std::wstring_view::npos) throw std::runtime_error("invalid Character entry");
        auto key = line.substr(0, split), value = line.substr(split + 1);
        while (!key.empty() && key.back() == L' ') key.remove_suffix(1);
        while (!value.empty() && value.front() == L' ') value.remove_prefix(1);
        while (!value.empty() && value.back() == L' ') value.remove_suffix(1);
        // Match the normal INI reader's optional surrounding quotes.
        if (value.size() >= 2 && ((value.front() == L'"' && value.back() == L'"') ||
            (value.front() == L'\'' && value.back() == L'\''))) {
            value.remove_prefix(1); value.remove_suffix(1);
        }
        if (!fields.emplace(lower(ascii(key)), ascii(value)).second)
            throw std::runtime_error("duplicate Character key");
    }
    return fields;
}
std::string get(const std::map<std::string, std::string>& fields, const char* key, std::string fallback = {}) {
    const auto found = fields.find(key);
    return found == fields.end() ? std::move(fallback) : found->second;
}
bool resourcePath(std::string_view value) {
    return !value.empty() && value.size() < 240 && value.front() != '/' &&
        value.find("..") == std::string_view::npos &&
        std::all_of(value.begin(), value.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                (ch >= '0' && ch <= '9') || ch == '_' || ch == '/' || ch == '.' || ch == '-';
        });
}
std::vector<std::string> commaList(std::string value, bool unique=true) {
    std::vector<std::string> result;
    while (!value.empty()) {
        const auto comma = value.find(',');
        auto item = value.substr(0, comma);
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) throw std::runtime_error("empty gameplay dependency");
        if (unique && std::find(result.begin(), result.end(), item) != result.end())
            throw std::runtime_error("duplicate gameplay dependency");
        result.push_back(std::move(item));
        if (result.size() > 64) throw std::runtime_error("at most 64 dependencies per list");
        if (comma == std::string::npos) break;
        value.erase(0, comma + 1);
        if (value.empty()) throw std::runtime_error("empty gameplay dependency");
    }
    return result;
}
CustomGameplay gameplaySection(const std::wstring& file, std::string_view actor) {
    const auto fields = characterSection(file, L"Character.Gameplay");
    CustomGameplay result;
    if (fields.empty()) return result;
    for (const auto& [key, _] : fields)
        if (key != "sourceactor" && key != "sourcedna" && key != "item" && key != "prop" &&
            key != "ability" && key != "preloadactors" && key != "preloadfiles" &&
            key != "itemcontrolsource" && key != "propcontrolsource" &&
            key != "itemdisplayname" && key != "propdisplayname" && key != "abilitydisplayname" &&
            key != "itemweaponsource" && key != "itemtooldna" && key != "itemtoolscript" &&
            key != "itemprojectileradius" && key != "itemprojectilespeed" && key != "itemprojectilelifetime" &&
            key != "itemprojectiledamage" && key != "itemprojectilescale" && key != "aimoffset" &&
            key != "inventorysource" && key != "inventoryexclude" && key != "preloadincludes")
            throw std::runtime_error("unknown Character.Gameplay key: " + key);
    result.sourceActor = get(fields, "sourceactor");
    result.sourceDna = get(fields, "sourcedna");
    result.inventorySource = get(fields, "inventorysource");
    result.inventoryExclude = commaList(get(fields, "inventoryexclude"));
    if (!result.inventorySource.empty() && !identifier(result.inventorySource))
        throw std::runtime_error("InventorySource must name a native avatar profile");
    if (!result.inventoryExclude.empty() && result.inventorySource.empty())
        throw std::runtime_error("InventoryExclude requires InventorySource");
    for (const auto& excluded : result.inventoryExclude)
        if (!identifier(excluded) || excluded == "ITEM_NONE" || excluded == "PROP_NONE" || excluded == "ABILITY_NONE")
            throw std::runtime_error("InventoryExclude needs tool IDs and must preserve the native NONE group entries");
    result.item = get(fields, "item"); result.prop = get(fields, "prop"); result.ability = get(fields, "ability");
    auto& weapon=result.itemWeapon;
    weapon.source=get(fields,"itemweaponsource");weapon.dna=get(fields,"itemtooldna");weapon.script=get(fields,"itemtoolscript");
    const auto number=[&](const char* key,double low,double high)->std::optional<double> {
        const auto value=get(fields,key);if(value.empty()) return {};
        char* end{};const double parsed=std::strtod(value.c_str(),&end);
        if(end!=value.c_str()+value.size() || !std::isfinite(parsed) || parsed<low || parsed>high)
            throw std::runtime_error(std::string("invalid gameplay number: ")+key);
        return parsed;
    };
    weapon.radius=number("itemprojectileradius",0.001,10);weapon.speed=number("itemprojectilespeed",0.1,1000);
    weapon.lifetime=number("itemprojectilelifetime",0.01,120);weapon.damage=number("itemprojectiledamage",0,10000);
    weapon.scale=number("itemprojectilescale",0.001,10);
    if(weapon.enabled()) {
        if(result.item.empty() || !identifier(weapon.source) || !resourcePath(weapon.dna) || !lower(weapon.dna).ends_with(".dnax") ||
           !resourcePath(weapon.script) || !lower(weapon.script).ends_with(".sx"))
            throw std::runtime_error("ItemWeaponSource needs Item, ItemToolDNA and ItemToolScript");
    } else if(!weapon.dna.empty() || !weapon.script.empty() || weapon.radius || weapon.speed || weapon.lifetime || weapon.damage || weapon.scale)
        throw std::runtime_error("projectile settings need ItemWeaponSource");
    const auto aim=get(fields,"aimoffset");
    if(!aim.empty()) {
        const auto values=commaList(aim,false);if(values.size()!=3) throw std::runtime_error("AimOffset requires three numbers");
        std::array<double,3> parsed{};
        for(unsigned i=0;i<3;++i) {
            char* end{};parsed[i]=std::strtod(values[i].c_str(),&end);
            if(end!=values[i].c_str()+values[i].size() || !std::isfinite(parsed[i]) || parsed[i]<-10 || parsed[i]>10)
                throw std::runtime_error("AimOffset components must be finite in -10..10");
        }
        result.aimOffset=parsed;
    }
    result.controlSources = {get(fields,"itemcontrolsource"),get(fields,"propcontrolsource")};
    result.toolDisplayNames = {get(fields,"itemdisplayname"),get(fields,"propdisplayname"),get(fields,"abilitydisplayname")};
    const std::array<const std::string*,3> slots{&result.item,&result.prop,&result.ability};
    for (unsigned i=0;i<3;++i) if (!result.toolDisplayNames[i].empty() &&
        (!printable(result.toolDisplayNames[i],255) || slots[i]->empty()))
        throw std::runtime_error("a tool display name needs its corresponding loadout slot and printable ASCII");
    for (unsigned i=0;i<2;++i) if (!result.controlSources[i].empty() &&
        (!identifier(result.controlSources[i]) || (i==0 ? result.item.empty() : result.prop.empty())))
        throw std::runtime_error("a control source needs a valid identifier and its corresponding loadout slot");
    if (!identifier(result.sourceActor) || lower(result.sourceActor) == lower(actor))
        throw std::runtime_error("SourceActor must name a different source avatar; use a private Actor/Gateway alias");
    if (!lower(actor).starts_with("lcr_"))
        throw std::runtime_error("private gameplay Actor/Gateway must start with LCR_ to avoid shipped actor names");
    if (!resourcePath(result.sourceDna) || !lower(result.sourceDna).ends_with(".dnax"))
        throw std::runtime_error("SourceDNA must be a relative .dnax asset path");
    if (result.item.empty() && result.prop.empty() && result.ability.empty())
        throw std::runtime_error("Character.Gameplay needs Item, Prop or Ability");
    for (const auto* tool : {&result.item, &result.prop, &result.ability})
        if (!tool->empty() && !identifier(*tool)) throw std::runtime_error("invalid gameplay tool identifier");
    result.preloadActors = commaList(get(fields, "preloadactors"));
    result.preloadIncludes = commaList(get(fields, "preloadincludes"));
    for (const auto& include : result.preloadIncludes)
        if (!resourcePath(include)) throw std::runtime_error("PreloadIncludes entries must be native dependency include names");
    for (const auto& dependency : result.preloadActors)
        if (!identifier(dependency)) throw std::runtime_error("PreloadActors entries must be actor identifiers");
    result.preloadFiles = commaList(get(fields, "preloadfiles"));
    for (const auto& dependency : result.preloadFiles) {
        const auto split = dependency.find('|');
        const auto kind = dependency.substr(0, split);
        if (split == std::string::npos || (kind != "DNA" && kind != "Script" && kind != "Entity" &&
            kind != "Cho" && kind != "Audio" && kind != "AnimTreeProject" && kind != "BehaviorMap") ||
            !resourcePath(dependency.substr(split + 1)))
            throw std::runtime_error("invalid typed PreloadFiles resource");
    }
    return result;
}
void validate(const CustomFigure& figure) {
    if (figure.sku < firstCustomSku || figure.sku > lastCustomSku)
        throw std::runtime_error("SkuId must be an explicit integer in 1000100..1999999");
    // The portal also uses Name to construct an asset path; display text has its own field.
    if (!identifier(figure.name)) throw std::runtime_error("Name must be a 1..127 byte identifier");
    if (!identifier(figure.gateway) || !identifier(figure.actor) || !identifier(figure.baseCharacter) ||
        !identifier(figure.playset)) throw std::runtime_error("invalid Gateway, Actor, BaseCharacter or Playset identifier");
    if (!stockName(figure.baseCharacter)) throw std::runtime_error("BaseCharacter must name a stock DI1 gateway figure");
    // Native creation resolves the registered gateway name directly. Until a separate actor
    // alias bridge exists, accepting a different Actor would register an unspawnable figure.
    if (lower(figure.actor) != lower(figure.gateway))
        throw std::runtime_error("Actor must match Gateway (case-insensitive)");
    if (reservedName(figure.gateway) || reservedName(figure.name))
        throw std::runtime_error("Name/Gateway conflicts with a stock DI1 figure");
    if (!printable(figure.displayName, 255) || !printable(figure.description, 1024, true) ||
        !printable(figure.icon, 255, true)) throw std::runtime_error("invalid display text, description or icon");
}

constexpr std::array<uint8_t, 16> kKey = {0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
const uint8_t kSbox[256] = {
    0x63,0x7C,0x77,0x7B,0xF2,0x6B,0x6F,0xC5,0x30,0x01,0x67,0x2B,0xFE,0xD7,0xAB,0x76,
    0xCA,0x82,0xC9,0x7D,0xFA,0x59,0x47,0xF0,0xAD,0xD4,0xA2,0xAF,0x9C,0xA4,0x72,0xC0,
    0xB7,0xFD,0x93,0x26,0x36,0x3F,0xF7,0xCC,0x34,0xA5,0xE5,0xF1,0x71,0xD8,0x31,0x15,
    0x04,0xC7,0x23,0xC3,0x18,0x96,0x05,0x9A,0x07,0x12,0x80,0xE2,0xEB,0x27,0xB2,0x75,
    0x09,0x83,0x2C,0x1A,0x1B,0x6E,0x5A,0xA0,0x52,0x3B,0xD6,0xB3,0x29,0xE3,0x2F,0x84,
    0x53,0xD1,0x00,0xED,0x20,0xFC,0xB1,0x5B,0x6A,0xCB,0xBE,0x39,0x4A,0x4C,0x58,0xCF,
    0xD0,0xEF,0xAA,0xFB,0x43,0x4D,0x33,0x85,0x45,0xF9,0x02,0x7F,0x50,0x3C,0x9F,0xA8,
    0x51,0xA3,0x40,0x8F,0x92,0x9D,0x38,0xF5,0xBC,0xB6,0xDA,0x21,0x10,0xFF,0xF3,0xD2,
    0xCD,0x0C,0x13,0xEC,0x5F,0x97,0x44,0x17,0xC4,0xA7,0x7E,0x3D,0x64,0x5D,0x19,0x73,
    0x60,0x81,0x4F,0xDC,0x22,0x2A,0x90,0x88,0x46,0xEE,0xB8,0x14,0xDE,0x5E,0x0B,0xDB,
    0xE0,0x32,0x3A,0x0A,0x49,0x06,0x24,0x5C,0xC2,0xD3,0xAC,0x62,0x91,0x95,0xE4,0x79,
    0xE7,0xC8,0x37,0x6D,0x8D,0xD5,0x4E,0xA9,0x6C,0x56,0xF4,0xEA,0x65,0x7A,0xAE,0x08,
    0xBA,0x78,0x25,0x2E,0x1C,0xA6,0xB4,0xC6,0xE8,0xDD,0x74,0x1F,0x4B,0xBD,0x8B,0x8A,
    0x70,0x3E,0xB5,0x66,0x48,0x03,0xF6,0x0E,0x61,0x35,0x57,0xB9,0x86,0xC1,0x1D,0x9E,
    0xE1,0xF8,0x98,0x11,0x69,0xD9,0x8E,0x94,0x9B,0x1E,0x87,0xE9,0xCE,0x55,0x28,0xDF,
    0x8C,0xA1,0x89,0x0D,0xBF,0xE6,0x42,0x68,0x41,0x99,0x2D,0x0F,0xB0,0x54,0xBB,0x16
};
const uint8_t kRcon[10] = {0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x1B,0x36};

// Doubling in GF(2^8): a shift, then the field's reduction polynomial when
// the high bit carried out.
uint8_t xtime(uint8_t a)
{
    return static_cast<uint8_t>((a << 1) ^ ((a & 0x80) ? 0x1B : 0x00));
}

// Multiplication in GF(2^8), by doubling and adding. Only used for the small
// MixColumns constants, so the loop is short.
uint8_t mul(uint8_t a, uint8_t b)
{
    uint8_t r = 0;
    while (b) {
        if (b & 1)
            r ^= a;
        a = xtime(a);
        b >>= 1;
    }
    return r;
}

using RoundKeys = std::array<std::array<uint8_t, 16>, 11>;

// FIPS-197 key schedule: 44 words derived from the 128-bit key, regrouped as
// eleven round keys.
RoundKeys expandKey()
{
    uint8_t w[44][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            w[i][j] = kKey[i * 4 + j];

    for (int i = 4; i < 44; ++i) {
        uint8_t t[4] = { w[i - 1][0], w[i - 1][1], w[i - 1][2], w[i - 1][3] };
        if (i % 4 == 0) {
            uint8_t tmp = t[0];
            t[0] = kSbox[t[1]];
            t[1] = kSbox[t[2]];
            t[2] = kSbox[t[3]];
            t[3] = kSbox[tmp];
            t[0] ^= kRcon[i / 4 - 1];
        }
        for (int j = 0; j < 4; ++j)
            w[i][j] = w[i - 4][j] ^ t[j];
    }

    RoundKeys rk{};
    for (int r = 0; r < 11; ++r)
        for (int c = 0; c < 4; ++c)
            for (int j = 0; j < 4; ++j)
                rk[r][c * 4 + j] = w[r * 4 + c][j];
    return rk;
}

// The schedule, expanded once. The key never varies, so neither does this.
const RoundKeys& roundKeys()
{
    static const RoundKeys rk = expandKey();
    return rk;
}

// One AES-128 block, encrypted in place. Encryption only -- nothing here ever
// needs to read a slot back.
void encryptBlock(uint8_t s[16])
{
    const RoundKeys& rk = roundKeys();

    for (int i = 0; i < 16; ++i)
        s[i] ^= rk[0][i];

    for (int round = 1; round <= 10; ++round) {
        for (int i = 0; i < 16; ++i)
            s[i] = kSbox[s[i]];

        // ShiftRows on the column-major state of FIPS-197.
        uint8_t t[16];
        for (int i = 0; i < 16; ++i)
            t[i] = s[(i + (i % 4) * 4) % 16];
        std::memcpy(s, t, 16);

        if (round != 10) {
            for (int c = 0; c < 4; ++c) {
                uint8_t* col = s + c * 4;
                uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                col[0] = static_cast<uint8_t>(mul(a0, 2) ^ mul(a1, 3) ^ a2 ^ a3);
                col[1] = static_cast<uint8_t>(a0 ^ mul(a1, 2) ^ mul(a2, 3) ^ a3);
                col[2] = static_cast<uint8_t>(a0 ^ a1 ^ mul(a2, 2) ^ mul(a3, 3));
                col[3] = static_cast<uint8_t>(mul(a0, 3) ^ a1 ^ a2 ^ mul(a3, 2));
            }
        }

        for (int i = 0; i < 16; ++i)
            s[i] ^= rk[round][i];
    }
}



std::string encryptTagged(std::uint8_t tag, std::string_view payload, std::uint32_t row, bool category = false) {
    std::string data(1, static_cast<char>(tag));
    data.append(payload);
    const auto pad = static_cast<char>(16 - data.size() % 16);
    data.append(static_cast<unsigned char>(pad), pad);
    std::uint8_t previous[16]{};
    if (!category) {
        constexpr char iv[] = "0123456789012345";
        for (unsigned i = 0; i < 16; ++i)
            previous[i] = static_cast<std::uint8_t>(iv[i]) ^ static_cast<std::uint8_t>(row >> ((i % 4) * 8));
    }
    for (std::size_t offset = 0; offset < data.size(); offset += 16) {
        std::uint8_t block[16];
        for (unsigned i = 0; i < 16; ++i) block[i] = static_cast<std::uint8_t>(data[offset + i]) ^ previous[i];
        encryptBlock(block);
        std::memcpy(previous, block, 16);
        std::memcpy(data.data() + offset, block, 16);
    }
    return data;
}
}

std::vector<CustomFigure> loadCustomFigures(const std::vector<mods::Mod>& mods, std::vector<std::string>& messages) {
    std::vector<CustomFigure> result;
    std::set<std::uint32_t> skus;
    std::set<std::string> names, gateways;
    for (const auto& mod : mods) {
        try {
            const auto fields = characterSection(mod.path + L"\\mod.ini");
            if (fields.empty()) continue;
            if (result.size() >= maximumFigures) throw std::runtime_error("at most 64 custom figures are supported");
            const auto skuText = get(fields, "skuid");
            if (skuText.size() != 7 || !std::all_of(skuText.begin(), skuText.end(),
                [](char ch) { return ch >= '0' && ch <= '9'; }))
                throw std::runtime_error("SkuId must be an explicit integer in 1000100..1999999");
            CustomFigure figure;
            figure.sku = static_cast<std::uint32_t>(std::stoul(skuText));
            figure.name = get(fields, "name");
            figure.gateway = get(fields, "gateway");
            figure.actor = get(fields, "actor", figure.gateway);
            figure.baseCharacter = get(fields, "basecharacter");
            figure.displayName = get(fields, "displayname", figure.name);
            figure.description = get(fields, "description");
            figure.icon = get(fields, "icon");
            figure.playset = get(fields, "playset", "RumpusRoom");
            figure.gameplay = gameplaySection(mod.path + L"\\mod.ini", figure.actor);
            figure.assetDirectory = get(fields, "assetdirectory");
            figure.modPath = mod.path;
            if (!figure.assetDirectory.empty() && (!resourcePath(figure.assetDirectory) ||
                lower(figure.assetDirectory) == "assets" || lower(figure.assetDirectory).starts_with("assets/") ||
                figure.assetDirectory.front() == '.' || figure.assetDirectory.ends_with('/')))
                throw std::runtime_error("AssetDirectory must be a relative mod folder outside the global assets folder");
            validate(figure);
            if (skus.count(figure.sku) || names.count(lower(figure.name)) || gateways.count(lower(figure.gateway)))
                throw std::runtime_error("duplicate custom SkuId, Name or Gateway");
            const auto toolActor=[](const CustomFigure& f) { return "lcr_"+std::to_string(f.sku)+"_item"; };
            if (figure.gameplay.itemWeapon.enabled() && lower(figure.actor)==toolActor(figure))
                throw std::runtime_error("custom actor conflicts with its private item actor");
            for (const auto& previous : result)
                if ((previous.gameplay.itemWeapon.enabled() && lower(figure.actor)==toolActor(previous)) ||
                    (figure.gameplay.itemWeapon.enabled() && lower(previous.actor)==toolActor(figure)))
                    throw std::runtime_error("custom actor conflicts with another figure's private item actor");
            // Publish only after all validation, so a rejected mod never reserves identities.
            skus.insert(figure.sku); names.insert(lower(figure.name)); gateways.insert(lower(figure.gateway));
            messages.push_back("Custom figure " + mod.folder + ": configured " + figure.name);
            result.push_back(std::move(figure));
        } catch (const std::exception& error) {
            messages.push_back("Custom figure " + mod.folder + ": REJECTED: " + error.what());
        }
    }
    return result;
}

std::string encryptFigureString(std::string_view value, std::uint32_t row) { return encryptTagged(2, value, row); }
std::string encryptFigureNumber(double value, std::uint32_t row) {
    static_assert(sizeof(double) == 8, "DI1 gateway numbers use IEEE-754 doubles");
    std::uint64_t bits{};
    std::memcpy(&bits, &value, sizeof(bits));
    char littleEndian[8];
    for (unsigned i = 0; i < 8; ++i) littleEndian[i] = static_cast<char>(bits >> (i * 8));
    return encryptTagged(1, std::string_view(littleEndian, 8), row);
}
std::string_view customFigureGatewayMarker() {
    static const std::string key = encryptTagged(2, "Category1", 0, true);
    return key;
}

std::string customFigureGatewayPatch(const std::vector<CustomFigure>& figures) {
    if (figures.empty()) return {};
    if (figures.size() > maximumFigures) throw std::runtime_error("too many custom figures");
    std::set<std::uint32_t> skus;
    std::set<std::string> names, gateways;
    for (const auto& figure : figures) {
        validate(figure);
        if (!skus.insert(figure.sku).second || !names.insert(lower(figure.name)).second ||
            !gateways.insert(lower(figure.gateway)).second)
            throw std::runtime_error("duplicate custom SkuId, Name or Gateway");
    }
    std::string source = R"lua(local _, _, log, environment = ...
local host = LiteralCrab
if type(host) == 'table' and type(host.ResetCustomFigures) == 'function' then
    host.ResetCustomFigures()
end
local markReady = type(host) == 'table' and host.MarkCustomFigureReady
local c = environment[)lua" +
        mods::luaQuote(customFigureGatewayMarker()) + "]\n";
    source += R"lua(if type(c) ~= 'table' then log('SKIPPED: custom figure Category1 table missing'); return end
local count = #c
if count < 38 or count > 102 then log('SKIPPED: unsupported Category1 row count'); return end
for index, row in pairs(c) do
    if type(index) ~= 'number' or index % 1 ~= 0 or index < 1 or index > count or type(row) ~= 'table' then
        log('SKIPPED: unsupported Category1 array shape'); return
    end
end
for index = 1, count do
    if type(c[index]) ~= 'table' then log('SKIPPED: noncontiguous Category1 array'); return end
end
local keys = {
)lua";
    for (std::uint32_t row = 1; row <= maximumRows; ++row) {
        source += "{";
        for (const auto field : {"id", "name", "displayName", "playset"})
            source += mods::luaQuote(encryptFigureString(field, row)) + ",";
        source += "},\n";
    }
    source += "}\nlocal figures = {\n";
    for (const auto& figure : figures) {
        source += "{sku=" + std::to_string(figure.sku) + ", label=" + mods::luaQuote(figure.name) + ", values={\n";
        for (std::uint32_t row = 1; row <= maximumRows; ++row) {
            source += "{" + mods::luaQuote(encryptFigureNumber(figure.sku, row)) + "," +
                mods::luaQuote(encryptFigureString(figure.gateway, row)) + "," +
                mods::luaQuote(encryptFigureString(figure.displayName, row)) + "," +
                mods::luaQuote(encryptFigureString(figure.playset, row)) + "},\n";
        }
        source += "}},\n";
    }
    source += R"lua(}
for _, figure in ipairs(figures) do
    local found, conflict = false, false
    for index, row in ipairs(c) do
        local key, value = keys[index], figure.values[index]
        if row[key[1]] == value[1] or row[key[2]] == value[2] then
            if found then conflict = true end
            found = true
            for field = 1, 4 do
                if row[key[field]] ~= value[field] then conflict = true end
            end
        end
    end
    if conflict then
        log('SKIPPED: conflicting custom figure ' .. figure.label)
    elseif found then
        if type(markReady) == 'function' then markReady(figure.sku) end
        log('UNCHANGED: custom figure ' .. figure.label)
    elseif #c >= 102 then
        log('SKIPPED: custom figure table full')
    else
        local index = #c + 1
        local row, key, value = {}, keys[index], figure.values[index]
        for field = 1, 4 do row[key[field]] = value[field] end
        c[index] = row
        if type(markReady) == 'function' then markReady(figure.sku) end
        log('APPLIED: custom figure ' .. figure.label)
    end
end
)lua";
    return source;
}
}
