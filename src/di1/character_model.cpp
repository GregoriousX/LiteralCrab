// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_model.hpp"
#include "di1/scene_format.hpp"
#include <algorithm>
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::string directoryOf(std::string_view path) { const auto slash = path.rfind('/'); return std::string(path.substr(0, slash == std::string_view::npos ? 0 : slash)); }
bool reference(const scene::Scene& s, const scene::Atom& atom) {
    const auto atomName = scene::name(s, atom);
    return (atomName == "Filename" || atomName == "SceneFilename") && scene::isText(atom) &&
        !lowerPath(scene::text(s, atom)).ends_with(".mtl"); // Materials live inside the model bundle.
}
std::size_t fileSets(const scene::Scene& s) {
    const auto index = scene::child(s, 0, "FileSets");
    if (index == scene::npos) throw std::runtime_error("entity has no FileSets");
    return index;
}
std::vector<std::pair<std::string, std::size_t>> groups(const scene::Scene& s) {
    std::vector<std::pair<std::string, std::size_t>> result;
    for (const auto index : scene::children(s, fileSets(s)))
        if (scene::name(s, s.atoms[index]) == "FileSet" && scene::isText(s.atoms[index]))
            result.emplace_back(std::string(scene::text(s, s.atoms[index])), index);
    return result;
}
scene::Scene identical(const std::string& bytes, std::string_view what) {
    auto decoded = scene::decode(bytes);
    if (scene::encode(decoded) != bytes) throw std::runtime_error("entity does not re-encode identically: " + std::string(what));
    return decoded;
}
std::set<std::string> references(const scene::Scene& s, std::string_view directory) {
    std::set<std::string> result;
    for (const auto& atom : s.atoms) if (reference(s, atom)) result.insert(joinPath(directory, scene::text(s, atom)));
    return result;
}
std::string hex8(std::string_view raw) {
    constexpr char digits[] = "0123456789abcdef";
    std::string text;
    for (unsigned char c : raw) { text += digits[c >> 4]; text += digits[c & 15]; }
    return text;
}
// Private texture key, distinct per namespace and source texture.
std::string textureKey(std::string_view namespaceKey, std::string_view canonical) {
    std::uint64_t hash = 14695981039346656037ull;
    for (const auto part : {std::string_view("LiteralCrab archive texture:"), namespaceKey, std::string_view(":"), canonical})
        for (unsigned char c : part) { hash ^= c; hash *= 1099511628211ull; }
    std::string raw(8, '\0');
    for (unsigned i = 0; i < 8; ++i) raw[i] = static_cast<char>(hash >> (8 * i));
    return raw;
}
std::string unhex8(std::string_view text) {
    std::string raw;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) raw += static_cast<char>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16));
    return raw;
}
}

ModelInfo inspectModel(NativeCatalog& catalog, std::string_view model) {
    ModelInfo info;
    const auto path = lowerPath(model);
    const auto members = catalog.members(path + ".zip");
    info.scene = members.contains(path + ".oct") || catalog.assets().member(path + ".oct");
    const auto bytes = catalog.tryRead(path + ".bent");
    if (!bytes) return info;
    info.entity = true;
    const auto entity = scene::decode(*bytes);
    for (const auto& [name, _] : groups(entity)) {
        info.groups.push_back(name);
        if (name == "Core:BaseMobility") info.playerSets = true;
    }
    info.motions = references(entity, directoryOf(path));
    if (const auto index = scene::child(entity, 0, "SceneFilename"); index != scene::npos)
        info.scene = info.scene || members.contains(joinPath(directoryOf(path), scene::text(entity, entity.atoms[index])));
    return info;
}

PlayerModel preparePlayerModel(NativeCatalog& catalog, mods::AssetIndex& assets, std::string_view namespaceKey,
    std::string_view sourceModel, std::string_view templateModel, AnimationMode mode, ResourceAliases& aliases) {
    PlayerModel result;
    if (mode == AnimationMode::Source) return result;
    const auto source = lowerPath(sourceModel), sourceDirectory = directoryOf(source);
    const auto sourceBent = catalog.tryRead(source + ".bent");
    std::optional<scene::Scene> entity;
    if (sourceBent) entity = identical(*sourceBent, source + ".bent");
    if (mode == AnimationMode::Auto) {
        bool player{};
        if (entity) for (const auto& [name, _] : groups(*entity)) player = player || name == "Core:BaseMobility";
        mode = player ? AnimationMode::Complete : AnimationMode::Template;
    }
    if (mode == AnimationMode::Complete && !entity) throw std::runtime_error("Animations=Complete needs a source entity (BENT): " + source);
    if (templateModel.empty()) throw std::runtime_error("player animations need a template model");
    const auto donorPath = lowerPath(templateModel), donorDirectory = directoryOf(donorPath);
    if (donorPath == source) return result;
    auto donor = identical(catalog.read(donorPath + ".bent"), donorPath + ".bent");
    const auto retarget = [&](scene::Scene& s, scene::Atom& atom, std::string_view from) {
        scene::setText(s, atom, relativePath(sourceDirectory, joinPath(from, scene::text(s, atom))));
    };
    scene::Scene built;
    if (mode == AnimationMode::Complete) {
        built = *entity;
        std::set<std::string> present;
        for (const auto& [name, _] : groups(built)) present.insert(name);
        const auto sets = fileSets(built), donorSets = fileSets(donor);
        std::vector<scene::Atom> appended;
        for (const auto& [name, index] : groups(donor)) {
            if (present.contains(name)) continue;
            result.addedGroups.push_back(name);
            for (auto at = index, end = scene::subtreeEnd(donor, index); at < end; ++at) {
                auto atom = donor.atoms[at];
                atom.depth = static_cast<std::uint16_t>(atom.depth - donor.atoms[donorSets].depth + built.atoms[sets].depth);
                atom.name = scene::intern(built, donor.strings[atom.name]);
                if (atom.kind == 0x05 || atom.kind == 0x0B || atom.kind == 0x0A || atom.kind == 0x4A)
                    for (auto& value : atom.values) value = scene::intern(built, donor.strings[value]);
                if (reference(built, atom)) retarget(built, atom, donorDirectory);
                appended.push_back(std::move(atom));
            }
        }
        if (appended.empty()) return result; // Nothing missing: the stock entity stays in use.
        built.atoms.insert(built.atoms.begin() + static_cast<std::ptrdiff_t>(scene::subtreeEnd(built, sets)), appended.begin(), appended.end());
    } else {
        // The template entity keeps its channels, bases, layout and animation
        // sets; only its mesh scene becomes the source's.
        std::string mesh;
        if (entity) {
            const auto index = scene::child(*entity, 0, "SceneFilename");
            if (index == scene::npos) throw std::runtime_error("source entity has no SceneFilename");
            mesh = joinPath(sourceDirectory, scene::text(*entity, entity->atoms[index]));
        } else mesh = source + ".oct";
        built = donor;
        for (auto& atom : built.atoms) if (reference(built, atom)) retarget(built, atom, donorDirectory);
        const auto scenePath = scene::child(built, 0, "SceneFilename");
        auto models = groups(built);
        const auto set = std::find_if(models.begin(), models.end(), [](const auto& g) { return g.first == "Models"; });
        if (scenePath == scene::npos || set == models.end()) throw std::runtime_error("template entity lacks SceneFilename or Models");
        const auto files = scene::child(built, set->second, "Files");
        const auto entries = files == scene::npos ? std::vector<std::size_t>{} : scene::children(built, files);
        if (entries.size() != 1) throw std::runtime_error("template entity must define exactly one model file");
        const auto filename = scene::child(built, entries[0], "Filename");
        if (filename == scene::npos || !scene::isText(built.atoms[filename])) throw std::runtime_error("template model file has no Filename");
        const auto relative = relativePath(sourceDirectory, mesh);
        scene::setText(built, built.atoms[scenePath], relative);
        scene::setText(built, built.atoms[filename], relative);
    }
    const auto bent = scene::encode(built);
    (void)identical(bent, "generated entity");

    // Every file the entity reaches, plus the source mesh's own sidecars.
    const auto sourceMembers = catalog.members(source + ".zip");
    const auto donorMembers = catalog.members(donorPath + ".zip");
    const auto locate = [&](const std::string& canonical) -> std::optional<mods::AssetMember> {
        mods::AssetMember member;
        if (const auto* replaced = assets.member(canonical)) return *replaced;
        for (const auto* list : {&sourceMembers, &donorMembers})
            if (const auto found = list->find(canonical); found != list->end()) {
                member.package = found->second.archive; member.entry = found->second.entry; return member;
            }
        return std::nullopt;
    };
    std::set<std::string> needed = references(built, sourceDirectory);
    std::vector<std::string> pending(needed.begin(), needed.end());
    std::vector<std::string> missing;
    while (!pending.empty()) {
        const auto path = pending.back(); pending.pop_back();
        if (!path.ends_with(".banm")) continue;
        const auto member = locate(path);
        if (!member) continue; // Reported below.
        std::string bytes;
        if (!assets.read(*member, bytes)) throw std::runtime_error("cannot read " + path);
        for (const auto& child : references(scene::decode(bytes), directoryOf(path)))
            if (needed.insert(child).second) pending.push_back(child);
    }
    for (const auto& [path, _] : sourceMembers) if (!path.ends_with(".bent")) needed.insert(path);
    const auto prefix = "characters/literalcrab/assets/" + std::string(namespaceKey) + "/";
    std::map<std::string, std::string> textures;
    for (const auto& path : needed)
        if (path.starts_with("textures/") && path.ends_with(".tbody") && path.size() == 31)
            textures.emplace(unhex8(path.substr(9, 16)), textureKey(namespaceKey, path));
    std::vector<mods::AssetMember> members;
    for (const auto& path : needed) {
        auto member = locate(path);
        if (!member) { missing.push_back(path); continue; }
        const auto target = path.starts_with("textures/") && textures.contains(unhex8(path.substr(9, 16)))
            ? "textures/" + hex8(textures.at(unhex8(path.substr(9, 16)))) + ".tbody" : prefix + path;
        if (assets.member(target)) continue; // An author's private file of the same name wins.
        if (path.ends_with(".mtb")) {
            std::string bytes;
            if (!assets.read(*member, bytes)) throw std::runtime_error("cannot read " + path);
            if (bytes.find("TEXB") != std::string::npos) rebindBundleTextures(bytes, textures);
            member = mods::AssetMember{};
            member->generated = std::make_shared<const std::string>(std::move(bytes));
        }
        member->name = target;
        members.push_back(std::move(*member));
    }
    if (!missing.empty())
        throw std::runtime_error(std::to_string(missing.size()) + " file(s) referenced by the player entity are missing, first: " + missing.front());
    if (members.size() > 4096) throw std::runtime_error("player entity exceeds 4096 files");
    const auto entityName = prefix + source + ".bent";
    if (assets.member(entityName)) throw std::runtime_error("private entity already supplied: " + entityName);
    mods::AssetMember generated;
    generated.name = entityName;
    generated.generated = std::make_shared<const std::string>(bent);
    members.push_back(std::move(generated));
    // Aliases are checked before anything is published.
    auto updated = aliases;
    for (const auto& [from, to] : {std::pair{source, prefix + source}, std::pair{source + ".bent", entityName}}) {
        const auto found = std::find_if(updated.begin(), updated.end(), [&](const auto& a) { return a.first == from; });
        if (found != updated.end() && found->second != to) throw std::runtime_error("private model alias conflict: " + from);
        if (found == updated.end()) updated.emplace_back(from, to);
    }
    result.files = members.size();
    result.mode = mode;
    assets.members.reserve(assets.members.size() + members.size());
    for (auto& member : members) assets.members.push_back(std::move(member));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    aliases = std::move(updated);
    return result;
}
}
