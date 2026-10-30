// SPDX-License-Identifier: GPL-3.0-only
#include "di1/toy_store_data.hpp"
#include <windows.h>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace crabe::di1 {
namespace {
// GameDB folders of the play sets with an inventory of their own, and the dictionary parts:
// the Toy Box and each world (Cars, Incredibles, Lone Ranger, Monsters U, Pirates, Toy Story).
constexpr std::string_view playsets[] = {"cars", "incredibles", "loneranger", "monsters", "toystoryinspace"};
constexpr std::string_view dictionaryParts[] = {"tb", "ca", "in", "lr", "mu", "pi", "ts"};
constexpr std::string_view dataMaps[] = {"core", "rumpusroom", "toystoryinspace", "loneranger", "incredibles",
    "pirates", "monstersu", "cars"};
// Monsters U loads what its bikes need with the play set, not with their actors: the ride
// interaction script (Incredibles and Cars lack it) and the set a crash breaks a bike into.
constexpr std::pair<std::string_view, std::string_view> playsetBikeFiles[] = {
    {"mu_bicycle", "Entity|vehicles/destructionbicycle"},
    {"mu_bicycle_mini", "Entity|vehicles/destructionbicyclemini"},
    {"mu_bicycle_sully", "Entity|vehicles/destructionbicyclesully"},
    {"mu_bicycle_randall", "Entity|vehicles/destructionbicyclerandall"}};
constexpr std::string_view bikeRideScript = "Script|scripts/aiinteractionriding.sx";

// A data-map record ("Actor" or "Include" group), searching core first.
const data::Table* dataMapRecord(NativeCatalog& catalog, std::string_view group, std::string_view name) {
    const auto key = lowerPath(name);
    for (const auto map : dataMaps) {
        const data::Value* records{};
        try { records = data::path(catalog.chunk("gamedb/core/" + std::string(map) + "datamap.lua"), {"DataMap", group}); }
        catch (const std::exception&) { continue; }
        if (!records || !records->isTable()) continue;
        for (const auto& [record, row] : records->table->fields) if (lowerPath(record) == key && row.isTable()) return row.table.get();
    }
    return nullptr;
}
// Every file of a record and of the records it includes, except models.
void dependencyFiles(NativeCatalog& catalog, std::string_view group, std::string_view name, std::set<std::string>& visited,
    std::set<std::string>& seen, std::vector<std::string>& files) {
    if (!visited.insert(lowerPath(group) + "|" + lowerPath(name)).second) return;
    const auto* record = dataMapRecord(catalog, group, name);
    if (!record) return;
    if (const auto* list = record->table("Files")) for (const auto& [_, file] : list->indices)
        if (file.isString() && !lowerPath(file.string).starts_with("entity|") && seen.insert(lowerPath(file.string)).second)
            files.push_back(file.string);
    if (const auto* includes = record->table("Includes")) for (const auto& [_, include] : includes->indices) {
        if (!include.isString()) continue;
        const auto bar = include.string.find('|');
        if (bar == std::string::npos) continue;
        auto kind = include.string.substr(bar + 1);
        kind = kind.substr(0, kind.find('|'));
        if (kind == "Include" || kind == "Actor") dependencyFiles(catalog, kind, include.string.substr(0, bar), visited, seen, files);
    }
}

// DICT: "DICT", version, build checksum, ?, entry count, ?; then 12-byte entries {relative
// string offset, flags, key hash}. A string starts one byte after the entry plus its offset.
std::map<std::uint32_t, std::string_view> dictionary(std::string_view file) {
    const auto word = [&](std::size_t at) {
        if (at + 4 > file.size()) throw std::runtime_error("truncated dictionary");
        std::uint32_t value{};
        std::memcpy(&value, file.data() + at, 4);
        return value;
    };
    if (file.size() < 24 || file.substr(0, 4) != "DICT") throw std::runtime_error("not a dictionary");
    const auto count = word(16);
    if (count > (file.size() - 24) / 12) throw std::runtime_error("dictionary entry count exceeds its size");
    std::map<std::uint32_t, std::string_view> result;
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = 24 + std::size_t{i} * 12, start = entry + word(entry) + 1;
        if (start >= file.size()) throw std::runtime_error("dictionary string outside the file");
        const auto end = file.find('\0', start);
        if (end == std::string_view::npos) throw std::runtime_error("unterminated dictionary string");
        result.try_emplace(word(entry + 8), file.substr(start, end - start));
    }
    return result;
}
bool named(const std::string& value) {
    const auto key = lowerPath(value);
    return !key.empty() && key != "none" && key != "na";
}
std::optional<std::string> fileBytes(const std::wstring& path) {
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return std::nullopt;
    LARGE_INTEGER size{};
    std::optional<std::string> result;
    if (GetFileSizeEx(file, &size) && size.QuadPart < (64ll << 20)) {
        std::string bytes(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read{};
        if (bytes.empty() || (ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size()))
            result = std::move(bytes);
    }
    CloseHandle(file);
    return result;
}
std::wstring wide(std::string_view value) { return std::wstring(value.begin(), value.end()); }
// The file the game would get: a mod's packaged member, a mod's disk replacement, a loose
// file in the game's assets folder, then the shipped archive.
std::optional<std::string> effective(NativeCatalog& catalog, const std::string& path) {
    if (catalog.assets().member(path)) return catalog.tryRead(path);
    if (const auto* target = catalog.assets().find(path)) return fileBytes(*target);
    if (!catalog.gameAssets().empty())
        if (auto loose = fileBytes(catalog.gameAssets() + L"\\" + wide(path))) return loose;
    return catalog.tryRead(path);
}
const data::Table* records(NativeCatalog& catalog, std::string_view chunk) {
    const auto* value = data::path(catalog.chunk(chunk), {"Data", "Records"});
    if (!value || !value->isTable()) throw std::runtime_error(std::string(chunk) + " has no Data.Records");
    return value->table.get();
}
}

std::map<std::string, std::string> readDictionaryTexts(std::string_view stringIds, std::string_view language,
    const std::set<std::string>& keys) {
    if (stringIds.size() < 12 || language.size() < 12 || stringIds.substr(8, 4) != language.substr(8, 4))
        throw std::runtime_error("language file is not of the key file's dictionary build");
    const auto ids = dictionary(stringIds), texts = dictionary(language);
    std::map<std::string, std::string> result;
    for (const auto& [hash, key] : ids) {
        if (!keys.contains(lowerPath(key))) continue;
        if (const auto text = texts.find(hash); text != texts.end() && !text->second.empty())
            result.emplace(lowerPath(key), std::string(text->second));
    }
    return result;
}

ToyStoreData readToyStoreData(NativeCatalog& catalog, std::vector<std::string>& messages) {
    ToyStoreData data;
    // Placeable objects: Core rows are loaded everywhere; a package's rows only in its world.
    std::map<std::string, const data::Value*> core;
    for (const auto& [_, row] : records(catalog, "gamedb/core/rumpusdata.lua")->indices)
        if (row.isTable()) core.emplace(lowerPath(row.table->text("Object Name")), &row);
    std::map<std::string, std::string> actors; // Object name -> its actor (the row's Filename).
    for (const auto& [_, row] : records(catalog, "gamedb/core/rumpusdata.lua")->indices)
        if (row.isTable()) actors.emplace(lowerPath(row.table->text("Object Name")), row.table->text("Filename"));
    std::map<std::string, const data::Value*> packaged;
    const auto addRows = [&](const std::string& chunk) {
        for (const auto& [_, row] : records(catalog, chunk)->indices)
            if (row.isTable() && packaged.emplace(lowerPath(row.table->text("Object Name")), &row).second)
                actors.emplace(lowerPath(row.table->text("Object Name")), row.table->text("Filename"));
    };
    addRows("gamedb/rumpusroom/rumpusdata.lua");
    for (const auto playset : playsets) try { addRows("gamedb/" + std::string(playset) + "/rumpusdata.lua"); }
        catch (const std::exception&) { /* A play set without object data adds none. */ }
    std::set<std::string> copied, placed;
    std::size_t playsetToys{};
    const auto addInventory = [&](const std::string& chunk, bool toyBox) {
        for (const auto& [_, row] : records(catalog, chunk)->indices) {
            if (!row.isTable()) continue;
            const auto& item = *row.table;
            const auto model = item.text("rroName"), key = lowerPath(model);
            const auto* enabled = item.field("enabled");
            // Trains run only on their tracks.
            if (item.text("type") != "CAT_VEHICLE" || !named(model) || (enabled && enabled->number == 0) ||
                key.find("train") != std::string::npos || !named(item.text("name"))) continue;
            const auto object = packaged.find(key);
            if (!core.contains(key) && object == packaged.end()) continue;
            const auto* sort = item.field("sortOrder");
            const auto card = item.text("srcRCCard");
            data.toys.push_back({item.text("name"), model, toyBox || !named(card) ? std::string{} : card, sort ? sort->number : 0, toyBox});
            playsetToys += toyBox ? 0 : 1;
            if (!core.contains(key) && copied.insert(key).second) data.objects += data::literal(*object->second) + ",";
            if (placed.insert(key).second) {
                const auto& source = core.contains(key) ? *core.at(key) : *object->second;
                data::Value copy = source;
                copy.table = std::make_shared<data::Table>(*source.table);
                const auto text = [&](const char* field, std::string value) {
                    auto& entry = copy.table->fields[field];
                    entry = data::Value{}; entry.type = data::Value::Type::String; entry.string = std::move(value);
                };
                auto modifier = copy.table->text("Modifier");
                if (lowerPath(modifier).find("m_nosave") == std::string::npos) modifier += modifier.empty() ? "M_NoSave" : "|M_NoSave";
                text("Object Name", toyStorePlacedName(model));
                text("Modifier", modifier);
                auto& id = copy.table->fields["Id"];
                id = data::Value{}; id.type = data::Value::Type::Number; id.number = 990000.0 + static_cast<double>(placed.size());
                data.objects += data::literal(copy) + ",";
            }
        }
    };
    addInventory("gamedb/rumpusroom/inventory__lua.lua", true);
    for (const auto playset : playsets) try { addInventory("gamedb/" + std::string(playset) + "/inventory__lua.lua", false); }
        catch (const std::exception&) { /* No inventory of its own. */ }
    data.objects = "{" + data.objects + "}";
    std::set<std::string> visited, seen;
    for (const auto& toy : data.toys)
        if (const auto actor = actors.find(lowerPath(toy.model)); actor != actors.end() && named(actor->second))
            dependencyFiles(catalog, "Actor", actor->second, visited, seen, data.dependencies);
    for (const auto& toy : data.toys) {
        const auto actor = actors.find(lowerPath(toy.model));
        if (actor == actors.end()) continue;
        for (const auto& [bike, file] : playsetBikeFiles) {
            if (lowerPath(actor->second) != bike) continue;
            for (const auto value : {bikeRideScript, file})
                if (seen.insert(lowerPath(std::string(value))).second) data.dependencies.emplace_back(value);
        }
    }
    for (const auto& toy : data.toys)
        for (const auto* suffix : {"_name", "_desc"}) {
            data.keys.insert(lowerPath(toy.name) + suffix);
            if (toy.card.empty()) continue;
            data.keys.insert(lowerPath(toy.card) + suffix);
            data.aliases.emplace(lowerPath(toy.card) + suffix, lowerPath(toy.name) + suffix);
        }
    std::set<std::string> probes;
    for (const auto probe : toyStoreProbeKeys) probes.emplace(probe);
    // Every language some world dictionary offers, from the shipped archives, mod files and
    // loose files alike; PC files are preferred over console ones of the same language.
    std::set<std::string> stems;
    const auto offer = [&](std::string name) {
        name = lowerPath(name);
        name = name.substr(name.rfind('/') + 1);
        for (const std::string_view suffix : {"_win32.dct", "_xbox360.dct"})
            if (name.ends_with(suffix) && !name.starts_with("stringids")) stems.insert(name.substr(0, name.size() - suffix.size()));
    };
    for (const auto part : dictionaryParts) {
        const auto folder = "lang/" + std::string(part) + "/";
        if (const auto archive = catalog.archive(folder + std::string(part) + ".zip"))
            for (const auto& entry : archive->entries()) offer(entry.name);
        for (const auto& member : catalog.assets().members) if (member.name.starts_with(folder)) offer(member.name);
        for (const auto& redirect : catalog.assets().redirects) if (redirect.path.starts_with(folder)) offer(redirect.path);
        if (!catalog.gameAssets().empty()) {
            WIN32_FIND_DATAW found{};
            const auto search = FindFirstFileW((catalog.gameAssets() + L"\\lang\\" + wide(part) + L"\\*.dct").c_str(), &found);
            if (search != INVALID_HANDLE_VALUE) {
                do {
                    std::string name;
                    for (const auto* c = found.cFileName; *c; ++c) name += *c < 128 ? static_cast<char>(*c) : '?';
                    offer(name);
                } while (FindNextFileW(search, &found));
                FindClose(search);
            }
        }
    }
    const auto read = [&](std::string_view part, const std::string& stem) {
        const auto folder = "lang/" + std::string(part) + "/";
        auto file = effective(catalog, folder + stem + "_win32.dct");
        return file ? file : effective(catalog, folder + stem + "_xbox360.dct");
    };
    std::map<std::string, std::optional<std::string>> ids;
    for (const auto part : dictionaryParts) ids[std::string(part)] = effective(catalog, "lang/" + std::string(part) + "/stringids_win32.dct");
    const auto mainIds = effective(catalog, "lang/main/stringids_win32.dct");
    std::set<std::string> skipped;
    for (const auto& stem : stems) {
        ToyStoreLanguage language{stem, {}, {}};
        for (const auto part : dictionaryParts) {
            const auto& partIds = ids[std::string(part)];
            const auto file = partIds ? read(part, stem) : std::nullopt;
            if (!file) continue;
            try { language.texts.merge(readDictionaryTexts(*partIds, *file, data.keys)); }
            catch (const std::exception&) { skipped.insert(stem); } // Another dictionary build.
        }
        if (language.texts.empty()) continue;
        if (const auto main = mainIds ? read("main", stem) : std::nullopt) try {
            language.probes = readDictionaryTexts(*mainIds, *main, probes);
        } catch (const std::exception&) { /* Recognised only by elimination. */ }
        data.languages.push_back(std::move(language));
    }
    std::string ignored;
    for (const auto& stem : skipped) ignored += (ignored.empty() ? "" : ", ") + stem;
    messages.push_back("ToyStore: " + std::to_string(data.toys.size() - playsetToys) + " Toy Box and " +
        std::to_string(playsetToys) + " play set vehicles/mounts, " + std::to_string(placed.size()) + " session-only and " +
        std::to_string(copied.size()) + " original object rows and " + std::to_string(data.dependencies.size()) + " dependency files for play sets, names in " +
        std::to_string(data.languages.size()) + " languages" +
        (ignored.empty() ? "." : "; dictionaries of another build skipped: " + ignored + "."));
    return data;
}
}
