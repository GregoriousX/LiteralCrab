// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_mods.hpp"
#include "di1/character_assets.hpp"
#include "di1/portrait_texture.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <map>
#include <stdexcept>

namespace crabe::di1 {
namespace {
struct Label { std::string key, descriptionKey, title, description; };
std::vector<Label> labels;
std::vector<std::uint32_t> configuredSkus;
std::array<std::atomic<bool>, 64> registered{};
std::array<std::atomic<bool>, 64> conflicted{};
// Configured before any game Lua chunk executes; callbacks never allocate or
// access a Lua state belonging to another script VM.
mods::LuaApi readinessApi;
std::size_t figureIndex(lua_State* state) {
    if (readinessApi.type(state, 1) != 3) return configuredSkus.size();
    const double sku = readinessApi.toNumber(state, 1);
    if (!std::isfinite(sku)) return configuredSkus.size();
    for (std::size_t i = 0; i < configuredSkus.size(); ++i)
        if (sku == configuredSkus[i]) return i;
    return configuredSkus.size();
}
int __cdecl resetFigures(lua_State*) {
    for (auto& ready : registered) ready.store(false, std::memory_order_relaxed);
    return 0;
}
int __cdecl markFigure(lua_State* state) {
    const auto index = figureIndex(state);
    if (index < configuredSkus.size()) registered[index].store(true, std::memory_order_release);
    return 0;
}
int __cdecl figureReady(lua_State* state) {
    const auto index = figureIndex(state);
    readinessApi.pushBoolean(state, index < configuredSkus.size() && registered[index].load(std::memory_order_acquire) &&
        !conflicted[index].load(std::memory_order_acquire));
    return 1;
}
int __cdecl disableFigure(lua_State* state) {
    const auto index = figureIndex(state);
    if (index < configuredSkus.size()) conflicted[index].store(true, std::memory_order_release);
    return 0;
}
std::string lower(std::string value) {
    for (auto& ch : value) {
        if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + 32);
        if (ch == '\\') ch = '/';
    }
    return value;
}
std::uint32_t word(const std::string& bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated portrait bundle");
    std::uint32_t result{};
    std::memcpy(&result, bytes.data() + at, 4);
    return result;
}
void put(std::string& bytes, std::size_t at, std::uint32_t value) {
    std::memcpy(bytes.data() + at, &value, 4);
}
std::string portraitBundle(std::string_view name, const std::array<char, 8>& hash) {
    const auto stringsEnd = (36 + name.size() + 1 + 3) & ~std::size_t(3);
    // TEXB v3 has a 12-byte entry: texture hash (8), string-table offset (4).
    // A short name can leave spare alignment padding, which must not be relied
    // on for the final field; longer names otherwise expose an out-of-bounds read.
    const auto size = (stringsEnd + 12 + 15) & ~std::size_t(15);
    std::string data(size, '\0');
    std::memcpy(data.data(), "BNDL", 4); put(data, 4, 1); put(data, 8, static_cast<std::uint32_t>(size)); put(data, 12, 1);
    std::memcpy(data.data() + 16, "TEXB", 4); put(data, 20, 3); put(data, 24, static_cast<std::uint32_t>(size - 16));
    put(data, 28, 1); put(data, 32, static_cast<std::uint32_t>(name.size() + 1));
    std::memcpy(data.data() + 36, name.data(), name.size());
    std::memcpy(data.data() + stringsEnd, hash.data(), hash.size());
    put(data, stringsEnd + 8, 0); // This entry's name begins at string-table offset zero.
    return data;
}
void generated(mods::AssetIndex& assets, std::string name, std::string bytes) {
    if (assets.member(name)) return; // An explicit mod portrait wins.
    mods::AssetMember item;
    item.name = std::move(name);
    item.generated = std::make_shared<const std::string>(std::move(bytes));
    assets.members.push_back(std::move(item));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
}
std::string texturePath(const std::array<char, 8>& hash) {
    constexpr char hex[] = "0123456789abcdef";
    std::string name = "textures/";
    for (unsigned char ch : hash) { name += hex[ch >> 4]; name += hex[ch & 15]; }
    return name + ".tbody";
}
std::array<char, 8> textureHash(std::string_view bytes) {
    // Private content address. The engine uses the eight bytes only to locate
    // the texture; generated files never replace the source icon's resource.
    std::uint64_t value = 14695981039346656037ull;
    for (unsigned char ch : bytes) { value ^= ch; value *= 1099511628211ull; }
    std::array<char, 8> hash;
    for (unsigned i = 0; i < 8; ++i) hash[i] = static_cast<char>(value >> (i * 8));
    return hash;
}
std::string definitions(const std::vector<CustomFigure>& figures) {
    std::string text = "local figures = {\n";
    for (const auto& f : figures)
        text += "{sku=" + std::to_string(f.sku) + ",name=" + mods::luaQuote(f.name) + ",gateway=" + mods::luaQuote(f.gateway) +
            ",actor=" + mods::luaQuote(f.actor) + ",base=" + mods::luaQuote(f.baseCharacter) +
            ",description=" + mods::luaQuote(f.description.empty() ? "" : f.name + "_description") + "},\n";
    return text + "}\n";
}
}

void prepareCustomCharacterAssets(mods::AssetIndex& assets, const std::wstring& gameAssets,
    std::vector<CustomFigure>& figures, std::vector<std::string>& messages) {
    std::map<std::string, std::shared_ptr<const mods::zip::Archive>> archives;
    std::erase_if(figures, [&](const CustomFigure& f) {
        try {
            auto icon = lower(f.icon);
            if (icon.empty()) throw std::runtime_error("Icon must name an existing ui/... portrait resource");
            if (icon.ends_with(".mtb")) icon.resize(icon.size() - 4);
            if (!icon.starts_with("ui/") || icon.find("..") != std::string::npos || icon.find(':') != std::string::npos)
                throw std::runtime_error("Icon must be a relative ui/... resource");
            const auto slash = icon.rfind('/');
            if (slash == std::string::npos || slash == 2) throw std::runtime_error("invalid Icon path");
            const auto directory = icon.substr(0, slash), leaf = icon.substr(slash + 1);
            std::string bundle;
            const auto* override = assets.member(customAssetPath(f,icon + ".mtb"));
            if (override && !assets.read(*override, bundle)) throw std::runtime_error("cannot read mod portrait");
            auto& archive = archives[directory];
            const auto original = [&]() -> const mods::zip::Archive& {
                if (archive) return *archive;
                auto path = directory + '/' + directory.substr(directory.rfind('/') + 1) + ".zip";
                archive = std::make_shared<const mods::zip::Archive>(gameAssets + L"/" + std::wstring(path.begin(), path.end()));
                return *archive;
            };
            if (!override) {
                const auto* entry = original().find(leaf + ".mtb");
                if (!entry) throw std::runtime_error("original portrait resource not found");
                bundle = original().read(*entry);
            }
            if (bundle.size() < 48 || bundle.compare(0, 4, "BNDL") || word(bundle, 4) != 1 || word(bundle, 12) != 1 ||
                bundle.compare(16, 4, "TEXB") || word(bundle, 20) != 3 || word(bundle, 28) != 1)
                throw std::runtime_error("unsupported portrait bundle format");
            const auto nameLength = word(bundle, 32);
            if (!nameLength || nameLength > 256) throw std::runtime_error("invalid portrait texture name");
            const auto hashOffset = (36u + nameLength + 3u) & ~3u;
            if (hashOffset > bundle.size() || bundle.size() - hashOffset < 12) throw std::runtime_error("portrait texture entry truncated");
            if (word(bundle, hashOffset + 8) != 0) throw std::runtime_error("unsupported portrait name offset");
            std::array<char, 8> hash;
            std::memcpy(hash.data(), bundle.data() + hashOffset, 8);
            const auto texture = texturePath(hash);
            std::string source;
            if (const auto* replacement = assets.member(texture)) {
                if (!assets.read(*replacement, source)) throw std::runtime_error("cannot read mod portrait texture");
            } else {
                const auto* entry = original().find(texture);
                if (!entry) throw std::runtime_error("portrait texture dependency not found");
                source = original().read(*entry);
            }
            // Prepare all variants before publishing any. General UI icons lack
            // the portal's transparent 512px canvas and otherwise fill its frame.
            std::vector<std::pair<std::string, std::string>> prepared;
            const auto target = lower(f.name);
            for (const auto suffix : {"", "_s", "_sm"}) {
                const auto name = target + suffix;
                const auto path = "ui/virtualreadercharacters/" + name + ".mtb";
                if (assets.member(path)) continue; // Keep an authored portal portrait unchanged.
                auto canvas = makePortalPortrait(source, std::string_view(suffix) == "_sm" ? 256 : 512,
                    std::string_view(suffix) == "_s");
                const auto canvasHash = textureHash(canvas);
                const auto canvasPath = texturePath(canvasHash);
                if (const auto* existing = assets.member(canvasPath)) {
                    std::string bytes;
                    if (!assets.read(*existing, bytes) || bytes != canvas)
                        throw std::runtime_error("generated portrait texture address conflicts with a mod resource");
                }
                prepared.emplace_back(canvasPath, std::move(canvas));
                prepared.emplace_back(path, portraitBundle(name, canvasHash));
            }
            for (auto& [path, bytes] : prepared) generated(assets, std::move(path), std::move(bytes));
            messages.push_back("Custom figure " + f.name + ": portal portraits fitted from " + icon);
            return false;
        } catch (const std::exception& error) {
            messages.push_back("Custom figure " + f.name + ": REJECTED portrait: " + error.what());
            return true;
        }
    });
}

void setCustomCharacterLabels(const std::vector<CustomFigure>& figures) {
    labels.clear();
    configuredSkus.clear();
    resetFigures(nullptr);
    for (auto& conflict : conflicted) conflict.store(false, std::memory_order_relaxed);
    for (const auto& f : figures) {
        if (configuredSkus.size() == registered.size()) break;
        labels.push_back({f.name, f.name + "_description", f.displayName, f.description});
        const std::array<const std::string*,3> tools{&f.gameplay.item,&f.gameplay.prop,&f.gameplay.ability};
        for (unsigned i=0;i<3;++i) if (!tools[i]->empty() && !f.gameplay.toolDisplayNames[i].empty()) {
            const auto key=*tools[i]+"_name";
            // Native localization keys belong to tool IDs. First enabled mod
            // wins when multiple figures use the same tool.
            if (std::none_of(labels.begin(),labels.end(),[&](const Label& entry){return entry.key==key;}))
                labels.push_back({key,{},f.gameplay.toolDisplayNames[i],{}});
        }
        configuredSkus.push_back(f.sku);
    }
}
void addCustomCharacterAdapters(mods::LuaApi& api) {
    if (configuredSkus.empty() || !api.type || !api.toNumber || !api.pushBoolean) return;
    readinessApi = api;
    api.adapters.push_back({"ResetCustomFigures", &resetFigures});
    api.adapters.push_back({"MarkCustomFigureReady", &markFigure});
    api.adapters.push_back({"IsCustomFigureReady", &figureReady});
    api.adapters.push_back({"DisableCustomFigure", &disableFigure});
}
const char* customCharacterText(const char* key) noexcept {
    if (!key) return nullptr;
    if (*key == '@') ++key;
    for (const auto& label : labels) {
        if (label.key == key) return label.title.c_str();
        if (!label.descriptionKey.empty() && label.descriptionKey == key) return label.description.c_str();
    }
    return nullptr;
}

std::string customCharacterCatalogPatch(const std::vector<CustomFigure>& figures) {
    if (figures.empty()) return {};
    return definitions(figures) + R"lua(
local result, _, log = ...
local data = type(result) == 'table' and result or VirtualReaderData
if type(data) ~= 'table' or type(data.SourceAvatarData) ~= 'table' then
    log('SKIPPED: custom character catalog missing'); return
end
local rows = data.SourceAvatarData
for _, figure in ipairs(figures) do
    local base, found, conflict
    for _, row in ipairs(rows) do
        if string.lower(row.Gateway or '') == string.lower(figure.base) or
           string.lower(row.Name or '') == string.lower(figure.base) then base = row end
        if row.Name == figure.name or row.Gateway == figure.gateway or row.sku_id == tostring(figure.sku) then
            if found or row.Name ~= figure.name or row.Gateway ~= figure.gateway or row.sku_id ~= tostring(figure.sku) then conflict = true end
            found = row
        end
    end
    if conflict or not base then log('SKIPPED: custom character catalog conflict/base ' .. figure.name)
    elseif not found then
        local row = {}
        for key, value in pairs(base) do row[key] = value end
        row.Name, row.Gateway, row.sku_id = figure.name, figure.gateway, tostring(figure.sku)
        row.available, row.Description = true, figure.description
        row.IAP, row.PCSKU, row.TrailerURL = '', '', ''
        row.Item1, row.Item2, row.HexCoin1, row.HexCoin2 = '', '', '', ''
        rows[#rows + 1] = row
        log('APPLIED: custom character catalog ' .. figure.name)
    end
end
)lua";
}

std::string customCharacterPortalPatch(const std::vector<CustomFigure>& figures) {
    if (figures.empty()) return {};
    return definitions(figures) + R"lua(
local result, _, log = ...
local portal = type(result) == 'table' and result or VirtualReader
if type(portal) ~= 'table' or portal.LiteralCrabCustomFigures then return end
if not LiteralCrab.has('TagCategory') or not LiteralCrab.has('IsCustomFigureReady') then
    log('SKIPPED: native custom figure query unavailable'); return
end
local byName, byGateway = {}, {}
for _, figure in ipairs(figures) do byName[figure.name], byGateway[figure.gateway] = figure, figure end
local query = LiteralCrab.TagCategory
local function ready(figure) return LiteralCrab.IsCustomFigureReady(figure.sku) and query(figure.sku) == 0 end
local avatarLocked, characterLocked, valid = portal.IsAvatarLocked, portal.CharacterLocked, portal.IsIGPValid
if type(avatarLocked) ~= 'function' or type(characterLocked) ~= 'function' or type(valid) ~= 'function' then
    log('SKIPPED: original custom character portal methods missing'); return
end
portal.IsAvatarLocked = function(self, gateway)
    local figure = byGateway[gateway]
    if figure then return not ready(figure) end
    return avatarLocked(self, gateway)
end
portal.CharacterLocked = function(self, name)
    local figure = byName[name]
    if figure then return not ready(figure) end
    return characterLocked(self, name)
end
portal.IsIGPValid = function(self, name)
    local figure = byName[name]
    if figure and not ready(figure) then return false end
    return valid(self, name)
end
portal.LiteralCrabCustomFigures = true
log('APPLIED: custom figure portal ownership; original native world validation retained')
)lua";
}

void addCustomCharacterPatches(mods::Index& index, const std::vector<CustomFigure>& figures) {
    if (figures.empty()) return;
    const auto marker = std::string("contains:") + std::string(customFigureGatewayMarker());
    index.addBuiltinPatch({"gateway.lua", marker}, "custom_figure_registry", customFigureGatewayPatch(figures), "{}");
    index.addBuiltinPatch({"presentation/virtualreaderdata.lua"}, "custom_character_catalog", customCharacterCatalogPatch(figures), "{}");
    index.addBuiltinPatch({"presentation/virtualreader.lua"}, "custom_character_portal", customCharacterPortalPatch(figures), "{}");
}
}
