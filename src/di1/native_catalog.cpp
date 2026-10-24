// SPDX-License-Identifier: GPL-3.0-only
#include "di1/native_catalog.hpp"
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::vector<std::string> segments(std::string_view path) {
    std::vector<std::string> result;
    std::size_t at{};
    while (at <= path.size()) {
        const auto end = path.find('/', at);
        const auto part = path.substr(at, end == std::string_view::npos ? path.size() - at : end - at);
        if (!part.empty()) result.emplace_back(part);
        if (end == std::string_view::npos) break;
        at = end + 1;
    }
    return result;
}
constexpr std::string_view actorLists[] = {"rumpusroom", "common", "toystoryinspace", "loneranger", "incredibles",
    "pirates", "monstersu", "cars"};
constexpr std::string_view dataMaps[] = {"core", "rumpusroom", "toystoryinspace", "loneranger", "incredibles",
    "pirates", "monstersu", "cars"};
}

std::string lowerPath(std::string_view value) {
    std::string result(value);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c == '\\') c = '/';
    }
    return result;
}
std::string joinPath(std::string_view directory, std::string_view relative) {
    auto parts = segments(lowerPath(directory));
    for (const auto& part : segments(lowerPath(relative))) {
        if (part == ".") continue;
        if (part == "..") {
            if (parts.empty()) throw std::runtime_error("resource reference escapes the asset tree");
            parts.pop_back();
        } else parts.push_back(part);
    }
    std::string result;
    for (const auto& part : parts) result += (result.empty() ? "" : "/") + part;
    return result;
}
std::string relativePath(std::string_view directory, std::string_view target) {
    const auto from = segments(lowerPath(directory)), to = segments(lowerPath(target));
    std::size_t common{};
    while (common < from.size() && common + 1 < to.size() && from[common] == to[common]) ++common;
    std::string result;
    for (auto i = common; i < from.size(); ++i) result += "../";
    for (auto i = common; i < to.size(); ++i) result += to[i] + (i + 1 < to.size() ? "/" : "");
    return result;
}

NativeCatalog::NativeCatalog(const mods::AssetIndex& assets, std::wstring gameAssets) : index(assets), root(std::move(gameAssets)) {}

std::shared_ptr<const mods::zip::Archive> NativeCatalog::archive(std::string_view path) {
    const auto key = lowerPath(path);
    if (const auto found = archives.find(key); found != archives.end()) return found->second;
    std::shared_ptr<const mods::zip::Archive> result;
    if (!root.empty()) try {
        result = std::make_shared<const mods::zip::Archive>(root + L"/" + std::wstring(key.begin(), key.end()));
    } catch (const std::exception&) { /* Absent or unsupported: remembered as missing. */ }
    archives.emplace(key, result);
    return result;
}

std::optional<std::string> NativeCatalog::tryRead(std::string_view requested) {
    const auto name = lowerPath(requested);
    if (const auto* member = index.member(name)) {
        std::string bytes;
        if (!index.read(*member, bytes)) throw std::runtime_error("cannot read mod resource " + name);
        return bytes;
    }
    for (auto slash = name.rfind('/'); slash != std::string::npos && slash; slash = name.rfind('/', slash - 1)) {
        const auto directory = name.substr(0, slash);
        const auto leaf = directory.substr(directory.rfind('/') + 1);
        if (const auto zip = archive(directory + '/' + leaf + ".zip")) {
            if (const auto* entry = zip->find(name.substr(slash + 1))) return zip->read(*entry);
            if (const auto* entry = zip->find(name)) return zip->read(*entry);
        }
    }
    // Startup archives hold resources with their full names (tool table, core data map).
    for (const auto* startup : {"startup5.zip", "startup2.zip", "startup.zip", "startup3.zip", "startup4.zip"})
        if (const auto zip = archive(startup))
            if (const auto* entry = zip->find(name)) return zip->read(*entry);
    return std::nullopt;
}

std::string NativeCatalog::read(std::string_view name) {
    if (auto bytes = tryRead(name)) return std::move(*bytes);
    throw std::runtime_error("resource not found: " + lowerPath(name));
}

const data::Globals& NativeCatalog::chunk(std::string_view name) {
    const auto key = lowerPath(name);
    if (const auto found = chunks.find(key); found != chunks.end()) return found->second;
    auto globals = data::evaluate(read(key));
    return chunks.emplace(key, std::move(globals)).first->second;
}

const NativeActor* NativeCatalog::actor(std::string_view name) {
    if (!actorRows) {
        actorRows.emplace();
        for (const auto list : actorLists) {
            const auto path = "gamedb/core/" + std::string(list) + "_actors.lua";
            const data::Table* records{};
            try { const auto* value = data::path(chunk(path), {"Data", "Records"}); records = value && value->isTable() ? value->table.get() : nullptr; }
            catch (const std::exception&) { continue; } // An unreadable list only removes candidates.
            if (!records) continue;
            for (const auto& [_, row] : records->indices) {
                if (!row.isTable()) continue;
                NativeActor actor{row.table->text("Name"), row.table->text("Type"), {}, row.table->text("Parms"), std::string(list)};
                if (actor.name.empty()) continue;
                if (const auto at = lowerPath(actor.parms).find("dnafile="); at != std::string::npos) {
                    auto value = actor.parms.substr(at + 8);
                    value = value.substr(0, value.find(';'));
                    while (!value.empty() && value.back() == ' ') value.pop_back();
                    while (!value.empty() && value.front() == ' ') value.erase(value.begin());
                    actor.dna = value;
                }
                actorRows->push_back(std::move(actor));
            }
        }
    }
    const auto key = lowerPath(name);
    for (const auto& actor : *actorRows) if (lowerPath(actor.name) == key) return &actor;
    return nullptr;
}

const data::Table* NativeCatalog::dependencies(std::string_view actorName) {
    const auto key = lowerPath(actorName);
    for (const auto map : dataMaps) {
        const auto path = "gamedb/core/" + std::string(map) + "datamap.lua";
        const data::Table* actors{};
        try { const auto* value = data::path(chunk(path), {"DataMap", "Actor"}); actors = value && value->isTable() ? value->table.get() : nullptr; }
        catch (const std::exception&) { continue; }
        if (!actors) continue;
        for (const auto& [name, row] : actors->fields) if (lowerPath(name) == key && row.isTable()) return row.table.get();
    }
    return nullptr;
}

ToolTableInfo NativeCatalog::tools() { return readToolTable(read("var/toolset/toolset.bin")); }

const data::Globals& NativeCatalog::weapons() { return chunk("var/weapons/weapons.lua"); }

std::map<std::string, ArchiveEntry> NativeCatalog::members(std::string_view archivePath) {
    std::map<std::string, ArchiveEntry> result;
    const auto zip = archive(archivePath);
    if (!zip) return result;
    const auto path = lowerPath(archivePath);
    const auto directory = path.substr(0, path.rfind('/'));
    for (std::size_t i = 0; i < zip->entries().size(); ++i) {
        const auto& entry = zip->entries()[i];
        if (entry.directory()) continue;
        const auto name = lowerPath(entry.name);
        const auto canonical = name.starts_with("characters/") || name.starts_with("textures/") ? name : directory + '/' + name;
        result.try_emplace(canonical, ArchiveEntry{zip, i});
    }
    return result;
}
}
