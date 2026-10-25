// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_model.hpp"
#include "di1/scene_format.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
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
// An entity's own archive: characters/mu_mike/mu_mike_ps.zip holds mu_mike_ps.bent.
std::string entityArchive(std::string_view path) { return std::string(path) + ".zip"; }
// A mod or folder-archive copy first, else the entity's own archive (play set variants).
std::string readEntity(NativeCatalog& catalog, const std::string& path) {
    if (auto bytes = catalog.tryRead(path + ".bent")) return std::move(*bytes);
    const auto zip = catalog.archive(entityArchive(path));
    const auto* entry = zip ? zip->find(path.substr(path.rfind('/') + 1) + ".bent") : nullptr;
    if (!entry) throw std::runtime_error("resource not found: " + path + ".bent");
    return zip->read(*entry);
}
std::string unhex8(std::string_view text) {
    std::string raw;
    for (std::size_t i = 0; i + 1 < text.size(); i += 2) raw += static_cast<char>(std::stoi(std::string(text.substr(i, 2)), nullptr, 16));
    return raw;
}
// Shared interactions a visitor plays from its own entity: the groups the
// shared playset animation provider fills (vehicles, mounts, ship, bike,
// boards) and LiteralCrab's own LCRM_ aliases such as the megaphone pose.
// A character's own powers and one-offs are never borrowed.
bool interactionGroup(std::string_view group) {
    for (const std::string_view shared : {"Mech:Ship", "Mech:Bike", "Mech:Rowboat", "Mech:RideHorse", "Mech:CarRide",
        "Mech:VehicleDrive", "Mech:VehicleDrive_SML", "Mech:Hoverboard", "Mech:JetPack_Glide", "Mech:Booster"})
        if (group == shared) return true;
    return false;
}
bool borrowedAlias(std::string_view group, std::string_view alias) {
    const auto key = lowerPath(alias);
    // The one-off groups hold a single shared environmental reaction each.
    return interactionGroup(group) || key.starts_with("lcrm_") ||
        (group == "Playset:MU_OneOffs" && key == "rct_stink_good") || (group == "Playset:Toy_OneOffs" && key == "rct_fartplant");
}
bool sameSubtree(const scene::Scene& a, std::size_t ai, const scene::Scene& b, std::size_t bi) {
    const auto aEnd = scene::subtreeEnd(a, ai), bEnd = scene::subtreeEnd(b, bi);
    if (aEnd - ai != bEnd - bi) return false;
    for (std::size_t i = 0; ai + i < aEnd; ++i) {
        const auto& x = a.atoms[ai + i];
        const auto& y = b.atoms[bi + i];
        if (x.depth - a.atoms[ai].depth != y.depth - b.atoms[bi].depth || x.kind != y.kind ||
            scene::name(a, x) != scene::name(b, y) || x.values.size() != y.values.size()) return false;
        const bool strings = x.kind == 0x05 || x.kind == 0x0B || x.kind == 0x0A || x.kind == 0x4A;
        for (std::size_t v = 0; v < x.values.size(); ++v)
            if (strings ? a.strings[x.values[v]] != b.strings[y.values[v]] : x.values[v] != y.values[v]) return false;
    }
    return true;
}
}

ModelInfo inspectModel(NativeCatalog& catalog, std::string_view model) {
    ModelInfo info;
    const auto path = lowerPath(model);
    const auto members = catalog.members(path + ".zip");
    info.scene = members.contains(path + ".oct") || catalog.assets().member(path + ".oct");
    const auto skeleton = [&](const std::string& scenePath) {
        // Best effort: an unreadable mesh scene only leaves the skeleton unknown.
        try {
            if (const auto mesh = catalog.tryRead(scenePath))
                for (const auto& text : scene::decode(*mesh).strings)
                    if (text.starts_with("Cntrl_")) info.controls.insert(text);
        } catch (const std::exception&) { info.controls.clear(); }
    };
    const auto bytes = catalog.tryRead(path + ".bent");
    if (!bytes) { skeleton(path + ".oct"); return info; }
    info.entity = true;
    const auto entity = scene::decode(*bytes);
    for (const auto& [name, _] : groups(entity)) {
        info.groups.push_back(name);
        if (name == "Core:BaseMobility") info.playerSets = true;
    }
    info.motions = references(entity, directoryOf(path));
    auto scenePath = path + ".oct";
    if (const auto index = scene::child(entity, 0, "SceneFilename"); index != scene::npos) {
        scenePath = joinPath(directoryOf(path), scene::text(entity, entity.atoms[index]));
        info.scene = info.scene || members.contains(scenePath);
    }
    skeleton(scenePath);
    return info;
}

std::set<std::string> shippedGroups(NativeCatalog& catalog, std::string_view model) {
    std::set<std::string> result;
    const auto path = lowerPath(model);
    const auto zip = catalog.archive(entityArchive(path));
    const auto* entry = zip ? zip->find(path.substr(path.rfind('/') + 1) + ".bent") : nullptr;
    if (!entry) return result;
    // Best effort: an unreadable entity only offers no native groups.
    try { for (const auto& [name, _] : groups(scene::decode(zip->read(*entry)))) result.insert(name); }
    catch (const std::exception&) { result.clear(); }
    return result;
}

std::map<std::string, std::array<double, 3>> bindPose(NativeCatalog& catalog, std::string_view model) {
    std::map<std::string, std::array<double, 3>> result;
    try {
        const auto bytes = catalog.tryRead(lowerPath(model) + ".oct");
        if (!bytes) return result;
        const auto s = scene::decode(*bytes);
        for (std::size_t i = 0; i < s.atoms.size(); ++i) {
            if (scene::name(s, s.atoms[i]) != "Influence") continue;
            const auto name = scene::child(s, i, "Name");
            const auto matrix = scene::child(s, i, "BindPoseSkinLocalToWorldMatrixInverseData");
            if (name == scene::npos || matrix == scene::npos || !scene::isText(s.atoms[name]) || s.atoms[matrix].values.size() != 16) continue;
            std::array<float, 16> m{};
            for (unsigned k = 0; k < 16; ++k) std::memcpy(&m[k], &s.atoms[matrix].values[k], 4);
            // Inverse bind (world to joint, row vectors): the joint's world position is -t R^T.
            std::array<double, 3> position{};
            for (unsigned axis = 0; axis < 3; ++axis)
                position[axis] = -(double(m[axis * 4]) * m[12] + double(m[axis * 4 + 1]) * m[13] + double(m[axis * 4 + 2]) * m[14]);
            result[lowerPath(scene::text(s, s.atoms[name]))] = position;
        }
    } catch (const std::exception&) { result.clear(); }
    const auto head = result.find("head");
    if (head == result.end() || head->second[1] <= 0) return {};
    const auto height = head->second[1];
    for (auto& [_, position] : result) for (auto& value : position) value /= height;
    return result;
}

double rigDistance(const std::map<std::string, std::array<double, 3>>& a, const std::map<std::string, std::array<double, 3>>& b) {
    static constexpr std::string_view joints[] = {"head", "neck", "spine1", "spine2", "clavicle_left", "clavicle_right",
        "shoulder_left", "shoulder_right", "elbow_left", "elbow_right", "hand_left", "hand_right", "knee_left", "knee_right",
        "ankle_left", "ankle_right", "ball_left", "ball_right"};
    double total{};
    unsigned shared{};
    for (const auto joint : joints) {
        const auto x = a.find(std::string(joint)), y = b.find(std::string(joint));
        if (x == a.end() || y == b.end()) continue;
        double square{};
        for (unsigned axis = 0; axis < 3; ++axis) square += (x->second[axis] - y->second[axis]) * (x->second[axis] - y->second[axis]);
        total += std::sqrt(square);
        ++shared;
    }
    return shared < 8 ? -1 : total / shared;
}

bool bipedSkeleton(const ModelInfo& model) {
    static constexpr std::string_view joints[] = {"Cntrl_Hips", "Cntrl_Head", "Cntrl_Clavicle_Left", "Cntrl_Clavicle_Right",
        "Cntrl_Elbow_Left", "Cntrl_Elbow_Right", "Cntrl_Hand_Left", "Cntrl_Hand_Right", "Cntrl_Knee_Left", "Cntrl_Knee_Right",
        "Cntrl_Ankle_Left", "Cntrl_Ankle_Right"};
    return std::all_of(std::begin(joints), std::end(joints), [&](auto joint) { return model.controls.contains(std::string(joint)); });
}

PlayerModel preparePlayerModel(NativeCatalog& catalog, mods::AssetIndex& assets, std::string_view namespaceKey,
    std::string_view sourceModel, std::string_view templateModel, AnimationMode mode, ResourceAliases& aliases,
    const std::vector<std::pair<std::string, std::string>>& groupDonors) {
    PlayerModel result;
    if (mode == AnimationMode::Source) return result;
    const auto source = lowerPath(sourceModel), sourceDirectory = directoryOf(source);
    const auto prefix = "characters/literalcrab/assets/" + std::string(namespaceKey) + "/";
    const auto entityName = prefix + source + ".bent";
    // An author's entity (AssetDirectory) is completed in place like a shipped one.
    const bool authored = assets.member(entityName) != nullptr;
    std::optional<std::string> sourceBent;
    if (authored) {
        std::string bytes;
        if (!assets.read(*assets.member(entityName), bytes)) throw std::runtime_error("cannot read " + entityName);
        sourceBent = std::move(bytes);
    } else sourceBent = catalog.tryRead(source + ".bent");
    std::optional<scene::Scene> entity;
    if (sourceBent) entity = identical(*sourceBent, source + ".bent");
    if (mode == AnimationMode::Complete && !entity) throw std::runtime_error("Animations=Complete needs a source entity (BENT): " + source);
    if (templateModel.empty()) throw std::runtime_error("player animations need a template model");
    const auto donorPath = lowerPath(templateModel), donorDirectory = directoryOf(donorPath);
    if (donorPath == source) return result;
    auto donor = identical(catalog.read(donorPath + ".bent"), donorPath + ".bent");
    // Animation entries index the entity's playback channels. Template groups
    // and aliases are only meaningful on an entity declaring the same channels.
    const auto channels = [](const scene::Scene& s) { return scene::child(s, 0, "Channels"); };
    const bool sameChannels = entity && channels(*entity) != scene::npos && channels(donor) != scene::npos &&
        sameSubtree(*entity, channels(*entity), donor, channels(donor));
    if (mode == AnimationMode::Auto) {
        bool player{};
        if (entity) for (const auto& [name, _] : groups(*entity)) player = player || name == "Core:BaseMobility";
        if (player && !sameChannels) {
            // A different playback layout (for example a mission giver's) cannot
            // take the template's sets: a shipped entity plays the template's
            // whole entity instead, and an author's entity stays as supplied.
            result.channelsDiffer = true;
            if (authored) return result;
            mode = AnimationMode::Template;
        } else mode = player ? AnimationMode::Complete : AnimationMode::Template;
    }
    const auto retarget = [&](scene::Scene& s, scene::Atom& atom, std::string_view from) {
        scene::setText(s, atom, relativePath(sourceDirectory, joinPath(from, scene::text(s, atom))));
    };
    scene::Scene built;
    // Copies the subtree at `index` of `from` (an entity in `directory`) as the last child of `parent`.
    const auto copyFrom = [&](const scene::Scene& from, std::string_view directory, std::size_t parent, std::size_t index) {
        std::vector<scene::Atom> appended;
        const auto depth = built.atoms[parent].depth + 1 - from.atoms[index].depth;
        for (auto at = index, end = scene::subtreeEnd(from, index); at < end; ++at) {
            auto atom = from.atoms[at];
            atom.depth = static_cast<std::uint16_t>(atom.depth + depth);
            atom.name = scene::intern(built, from.strings[atom.name]);
            if (atom.kind == 0x05 || atom.kind == 0x0B || atom.kind == 0x0A || atom.kind == 0x4A)
                for (auto& value : atom.values) value = scene::intern(built, from.strings[value]);
            if (reference(built, atom)) retarget(built, atom, directory);
            appended.push_back(std::move(atom));
        }
        built.atoms.insert(built.atoms.begin() + static_cast<std::ptrdiff_t>(scene::subtreeEnd(built, parent)), appended.begin(), appended.end());
    };
    const auto group = [](const scene::Scene& s, std::string_view name) {
        for (const auto& [value, index] : groups(s)) if (value == name) return index;
        return scene::npos;
    };
    // Interaction groups with their own stock donor (AnimationDonors or the
    // build preference). All shipped player entities share one channel layout.
    struct Donor { std::string group, path; scene::Scene entity; };
    std::vector<Donor> donors;
    for (const auto& [name, model] : groupDonors) {
        const auto path = lowerPath(model);
        if (path == donorPath || path == source) continue;
        auto other = identical(readEntity(catalog, path), path + ".bent");
        if (channels(other) == scene::npos || !sameSubtree(donor, channels(donor), other, channels(other)) || group(other, name) == scene::npos) {
            result.skippedDonors.push_back(name);
            continue;
        }
        donors.push_back({name, path, std::move(other)});
    }
    const auto donorFor = [&](std::string_view name) -> const Donor* {
        for (const auto& d : donors) if (d.group == name) return &d;
        return nullptr;
    };
    // Adds the shared aliases of `from`'s group that the built group lacks.
    const auto addAliases = [&](const scene::Scene& from, std::string_view directory, std::string_view name) {
        const auto fromGroup = group(from, name);
        const auto fromFiles = fromGroup == scene::npos ? scene::npos : scene::child(from, fromGroup, "Files");
        if (fromFiles == scene::npos) return;
        for (const auto entry : scene::children(from, fromFiles)) {
            const auto& atom = from.atoms[entry];
            if (scene::name(from, atom) != "File" || !scene::isText(atom) || !borrowedAlias(name, scene::text(from, atom))) continue;
            const auto target = group(built, name) == scene::npos ? scene::npos : scene::child(built, group(built, name), "Files");
            if (target == scene::npos) return;
            const auto alias = lowerPath(scene::text(from, atom));
            const auto existing = scene::children(built, target);
            if (std::any_of(existing.begin(), existing.end(), [&](auto at) {
                return scene::isText(built.atoms[at]) && lowerPath(scene::text(built, built.atoms[at])) == alias; })) continue;
            copyFrom(from, directory, target, entry);
            ++result.addedAliases;
        }
    };
    if (mode == AnimationMode::Complete) {
        built = *entity;
        std::set<std::string> present;
        for (const auto& [name, _] : groups(built)) present.insert(name);
        for (const auto& [name, index] : groups(donor)) {
            if (present.contains(name)) continue;
            result.addedGroups.push_back(name);
            if (const auto* d = donorFor(name)) {
                copyFrom(d->entity, directoryOf(d->path), fileSets(built), group(d->entity, name));
                result.donatedGroups.push_back(name + "<-" + d->path);
            } else copyFrom(donor, donorDirectory, fileSets(built), index);
        }
        // Shared interaction aliases missing from groups the entity already
        // defines. Their entries index the playback channels, so both entities
        // must declare identical channels; existing mappings are never changed.
        if (sameChannels) {
            for (const auto& [name, index] : groups(donor)) {
                if (std::find(result.addedGroups.begin(), result.addedGroups.end(), name) != result.addedGroups.end()) continue;
                if (const auto* d = donorFor(name)) addAliases(d->entity, directoryOf(d->path), name);
                else addAliases(donor, donorDirectory, name);
            }
        } else result.channelsDiffer = true;
        // Nothing missing: the stock or authored entity stays in use as is.
        if (result.addedGroups.empty() && !result.addedAliases) return result;
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
        // A borrowed whole entity takes each donated interaction group whole.
        for (const auto& d : donors) {
            if (const auto existing = group(built, d.group); existing != scene::npos)
                built.atoms.erase(built.atoms.begin() + static_cast<std::ptrdiff_t>(existing),
                    built.atoms.begin() + static_cast<std::ptrdiff_t>(scene::subtreeEnd(built, existing)));
            copyFrom(d.entity, directoryOf(d.path), fileSets(built), group(d.entity, d.group));
            result.donatedGroups.push_back(d.group + "<-" + d.path);
        }
    }
    const auto bent = scene::encode(built);
    (void)identical(bent, "generated entity");

    // Every file the entity reaches, plus the source mesh's own sidecars.
    const auto sourceMembers = catalog.members(source + ".zip");
    std::vector<std::map<std::string, ArchiveEntry>> lists{catalog.members(donorPath + ".zip")};
    for (const auto& d : donors) lists.push_back(catalog.members(entityArchive(d.path)));
    const auto locate = [&](const std::string& canonical) -> std::optional<mods::AssetMember> {
        mods::AssetMember member;
        // An author's AssetDirectory file (for example a custom mesh) is already mounted privately.
        if (const auto* authored = assets.member(prefix + canonical)) return *authored;
        if (const auto* replaced = assets.member(canonical)) return *replaced;
        if (const auto found = sourceMembers.find(canonical); found != sourceMembers.end()) {
            member.package = found->second.archive; member.entry = found->second.entry; return member;
        }
        for (const auto& list : lists)
            if (const auto found = list.find(canonical); found != list.end()) {
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
    // An author's mesh and sidecars are already mounted; a reference the
    // author's own entity left unresolved keeps its previous behavior.
    std::set<std::string> tolerated;
    if (authored) tolerated = references(*entity, sourceDirectory);
    else for (const auto& [path, _] : sourceMembers) if (!path.ends_with(".bent")) needed.insert(path);
    std::map<std::string, std::string> textures;
    for (const auto& path : needed)
        if (path.starts_with("textures/") && path.ends_with(".tbody") && path.size() == 31)
            textures.emplace(unhex8(path.substr(9, 16)), textureKey(namespaceKey, path));
    std::vector<mods::AssetMember> members;
    for (const auto& path : needed) {
        auto member = locate(path);
        if (!member) { if (!tolerated.contains(path)) missing.push_back(path); continue; }
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
    if (!authored) {
        mods::AssetMember generated;
        generated.name = entityName;
        generated.generated = std::make_shared<const std::string>(bent);
        members.push_back(std::move(generated));
    }
    // Aliases are checked before anything is published.
    auto updated = aliases;
    for (const auto& [from, to] : {std::pair{source, prefix + source}, std::pair{source + ".bent", entityName}}) {
        const auto found = std::find_if(updated.begin(), updated.end(), [&](const auto& a) { return a.first == from; });
        if (found != updated.end() && found->second != to) throw std::runtime_error("private model alias conflict: " + from);
        if (found == updated.end()) updated.emplace_back(from, to);
    }
    result.files = members.size() + (authored ? 1 : 0);
    result.mode = mode;
    if (authored) {
        // Replace the mounted entity's bytes; its unchanged disk file is no longer served.
        for (auto& member : assets.members)
            if (member.name == entityName) { member = mods::AssetMember{}; member.name = entityName; member.generated = std::make_shared<const std::string>(bent); }
        std::erase_if(assets.redirects, [&](const mods::AssetRedirect& redirect) { return redirect.path == entityName; });
    }
    assets.members.reserve(assets.members.size() + members.size());
    for (auto& member : members) assets.members.push_back(std::move(member));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    aliases = std::move(updated);
    return result;
}
}
