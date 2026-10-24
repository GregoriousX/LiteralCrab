// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_profile.hpp"
#include "di1/character_model.hpp"
#include <algorithm>
#include <cstring>
#include <set>
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::string lower(std::string_view value) { return lowerPath(value); }
std::string text(const data::Table& table, std::initializer_list<std::string_view> keys) {
    const auto* value = data::path(table, keys);
    return value && value->isString() ? value->string : std::string{};
}
const data::Table* dnaTable(NativeCatalog& catalog, const std::string& path) {
    const auto* dna = catalog.chunk(path).table("DNA");
    if (!dna) throw std::runtime_error("DNA file defines no DNA table: " + path);
    return dna;
}
// Player avatars (bipeds and cars) carry the avatar gadget, the controller
// mapping and a tool manager. NPCs (BipedNPC, Cowboy.btx...) do not.
bool playerForm(const data::Table& dna) {
    return data::path(dna, {"AgentGadgets", "Avatar"}) && data::path(dna, {"AgentGadgets", "ControlMapper"}) &&
        data::path(dna, {"Components", "ToolSetManager"});
}
// Template bodies and animation sets are biped rigs: only biped sources
// (player Biped or BipedNPC animation trees) are converted automatically.
bool biped(const data::Table& dna) {
    const auto project = lower(text(dna, {"AgentGadgets", "AnimTree", "AnimTreeProjectFile"}));
    return project == "biped.animtreeproject" || project == "bipednpc.animtreeproject";
}
struct Template {
    std::string gateway, actor, dna, mask, model, pieces, overrides;
    std::array<std::string, 3> start;
};
Template loadTemplate(NativeCatalog& catalog, std::string_view gateway) {
    if (!templateFigure(gateway)) throw std::runtime_error("Template must name a playable stock biped: " + std::string(gateway));
    const auto* actor = catalog.actor(lower(gateway));
    if (!actor || actor->type != "Avatar" || actor->dna.empty())
        throw std::runtime_error("template avatar row missing: " + lower(gateway));
    const auto* dna = dnaTable(catalog, actor->dna);
    Template result{std::string(gateway), lower(gateway), actor->dna,
        text(*dna, {"AgentGadgets", "Identification", "ActorTypeMask"}), text(*dna, {"AgentGadgets", "EntityInstance", "ModelName"}),
        text(*dna, {"AgentGadgets", "DynamicExploder", "dynexp_pieceFile"}), text(*dna, {"AgentGadgets", "AnimTree", "OverrideFile"}), {}};
    for (unsigned i = 0; i < 3; ++i) result.start[i] = text(*dna, {"Components", "ToolSetManager", i == 0 ? "AutoStartTool1" : i == 1 ? "AutoStartTool2" : "AutoStartTool3"});
    if (result.mask.empty() || result.model.empty()) throw std::runtime_error("template DNA lacks identity or model: " + result.dna);
    return result;
}
std::string chooseTemplate(NativeCatalog& catalog, const CustomFigure& f, const std::string& model, std::vector<std::string>& messages) {
    if (!f.gameplay.templateFigure.empty()) return f.gameplay.templateFigure;
    if (!f.baseCharacter.empty() && templateFigure(f.baseCharacter)) return f.baseCharacter;
    // Nearest stock rig: the template whose entity shares the most animation files.
    if (!model.empty()) {
        const auto source = inspectModel(catalog, model);
        std::string best;
        std::size_t score{};
        if (source.entity) for (const auto candidate : templateFigures()) {
            try {
                const auto tpl = loadTemplate(catalog, candidate);
                const auto other = inspectModel(catalog, tpl.model);
                const auto shared = static_cast<std::size_t>(std::count_if(source.motions.begin(), source.motions.end(),
                    [&](const auto& path) { return other.motions.contains(path); }));
                if (shared > score) { score = shared; best = std::string(candidate); }
            } catch (const std::exception&) { /* An unreadable template is not a candidate. */ }
        }
        if (!best.empty()) {
            messages.push_back("Custom gameplay " + f.name + ": Template " + best + " chosen automatically (" +
                std::to_string(score) + " shared animation files); set Template to override");
            return best;
        }
    }
    messages.push_back("Custom gameplay " + f.name + ": Template AV_Buzz (documented default); set Template to override");
    return "AV_Buzz";
}
// Actor types in the core PlayerControllable filter; empty when unreadable.
std::set<std::string> playerTypes(NativeCatalog& catalog) {
    std::set<std::string> result;
    try {
        const auto* sets = data::path(catalog.chunk("gamedb/core/bitmasks.lua"), {"Data", "Sets"});
        if (sets && sets->isTable()) for (const auto& [_, set] : sets->table->indices) {
            const auto* masks = set.isTable() ? set.table->table("filters") : nullptr;
            if (masks) for (const auto& [__, mask] : masks->indices)
                if (mask.isTable() && mask.table->text("filter") == "PlayerControllable") {
                    const auto desc = mask.table->text("desc");
                    for (std::size_t at = 0; at <= desc.size();) {
                        const auto end = std::min(desc.find('|', at), desc.size());
                        if (end > at) result.insert(lower(desc.substr(at, end - at)));
                        at = end + 1;
                    }
                }
        }
    } catch (const std::exception&) { result.clear(); }
    return result;
}
bool inProfile(const ToolTableInfo& tools, const std::vector<std::uint32_t>* profile, std::string_view id) {
    if (!profile) return false;
    return std::any_of(profile->begin(), profile->end(), [&](auto row) { return row < tools.tools.size() && tools.tools[row].id == id; });
}
const char* slotGroup(unsigned slot) { return slot == 0 ? "Items" : slot == 1 ? "Props" : "Abilities"; }
std::string& slot(CustomGameplay& g, unsigned index) { return index == 0 ? g.item : index == 1 ? g.prop : g.ability; }
void appendUnique(std::vector<std::string>& list, std::string value) {
    if (std::none_of(list.begin(), list.end(), [&](const auto& old) { return lower(old) == lower(value); }) && list.size() < 64)
        list.push_back(std::move(value));
}
}

// Projectile visuals named by a gun's projectile, when the shipped resource exists.
std::vector<std::string> weaponEffects(NativeCatalog& catalog, std::string_view gun) {
    std::vector<std::string> files;
    // Best effort: without a readable weapons table the author's PreloadFiles apply alone.
    const data::Globals* table{};
    try { table = &catalog.weapons(); } catch (const std::exception&) { return files; }
    const auto& weapons = *table;
    const auto find = [&](const char* list, std::string_view name) -> const data::Table* {
        const auto* rows = weapons.table(list);
        if (rows) for (const auto& [_, row] : rows->indices)
            if (row.isTable() && lower(row.table->text("name")) == lower(name)) return row.table.get();
        return nullptr;
    };
    const auto* weapon = find("weapon", gun);
    const auto* projectile = weapon ? find("proj", weapon->text("projectile")) : nullptr;
    if (!projectile) return files;
    if (const auto model = lower(projectile->text("inAirModel")); !model.empty()) {
        const auto members = catalog.members("weapons/weapons.zip");
        if (members.contains("weapons/" + model + ".oct") || members.contains("weapons/" + model + ".bent"))
            files.push_back("Entity|weapons/" + model);
    }
    for (const auto* field : {"projectileChoName", "impactChoName", "muzzleChoName"}) {
        const auto cho = lower(projectile->text(field));
        if (cho.size() < 2 || cho.find('/') != std::string::npos) continue;
        if (catalog.archive("choreographies/" + cho.substr(0, 2) + "/" + cho + ".zip"))
            files.push_back("Cho|choreographies/" + cho + "/" + cho);
    }
    return files;
}

std::vector<std::string> slngLiterals(std::string_view bytes) {
    const auto word = [&](std::size_t at) {
        if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated native script");
        std::uint32_t value{}; std::memcpy(&value, bytes.data() + at, 4); return value;
    };
    if (bytes.size() < 0x68 || bytes.substr(0, 12) != std::string_view("SLNG\x0c\x10\0\0\x78\x56\x34\x12", 12) ||
        word(12) != bytes.size()) throw std::runtime_error("unsupported native tool script");
    const auto count = word(0x30), offset = word(0x64);
    const auto list = 0x65ull + offset;
    if (!offset || count > 65535 || list > bytes.size() || count * 4ull > bytes.size() - list)
        throw std::runtime_error("invalid native script literal table");
    std::vector<std::string> result;
    for (unsigned i = 0; i < count; ++i) {
        const auto entry = static_cast<std::size_t>(list + i * 4ull);
        const auto relative = word(entry);
        if (!relative) continue;
        const auto start = entry + static_cast<std::uint64_t>(relative) + 1;
        if (start >= bytes.size()) throw std::runtime_error("invalid native script literal");
        const auto end = bytes.find('\0', static_cast<std::size_t>(start));
        if (end == std::string_view::npos) throw std::runtime_error("unterminated native script literal");
        result.emplace_back(bytes.substr(static_cast<std::size_t>(start), end - static_cast<std::size_t>(start)));
    }
    return result;
}

ToolSource inferToolSource(NativeCatalog& catalog, const ToolRowInfo& tool, const ResourceAliases& aliases, bool wantWeapon) {
    if (tool.actors.size() != 1 || tool.actors[0].empty())
        throw std::runtime_error(tool.id + " has " + std::to_string(tool.actors.size()) + " tool actors; exactly one is needed to copy it");
    ToolSource result;
    result.actor = lower(tool.actors[0]);
    const auto* actor = catalog.actor(result.actor);
    if (!actor || actor->dna.empty()) throw std::runtime_error("tool actor row missing: " + result.actor);
    result.dna = actor->dna;
    const auto* dna = dnaTable(catalog, privateAssetPath(aliases, result.dna));
    result.script = text(*dna, {"AgentGadgets", "Script", "ScriptName"});
    if (!wantWeapon) return result;
    if (result.script.empty()) throw std::runtime_error(tool.id + " has no native script, so it fires no gun");
    const auto literals = slngLiterals(catalog.read(privateAssetPath(aliases, result.script)));
    std::vector<std::string> guns;
    if (const auto* rows = catalog.weapons().table("weapon"))
        for (const auto& [_, row] : rows->indices)
            if (row.isTable() && row.table->text("class") == "gun" &&
                std::find(literals.begin(), literals.end(), row.table->text("name")) != literals.end())
                appendUnique(guns, row.table->text("name"));
    if (guns.size() != 1) throw std::runtime_error(tool.id + "'s script names " + std::to_string(guns.size()) +
        " native guns; set the weapon source explicitly");
    result.weapon = guns[0];
    return result;
}

void resolveCustomCharacter(NativeCatalog& catalog, mods::AssetIndex& assets, CustomFigure& f, std::vector<std::string>& messages) {
    auto& g = f.gameplay;
    const auto note = [&](const std::string& message) { messages.push_back("Custom gameplay " + f.name + ": " + message); };
    const NativeActor* sourceRow = g.sourceActor.empty() ? nullptr : catalog.actor(g.sourceActor);
    if (g.sourceDna.empty()) {
        if (!sourceRow) throw std::runtime_error("Source actor '" + g.sourceActor + "' is not in the native actor lists; "
            "give Source as a .dnax path instead");
        if (sourceRow->dna.empty()) throw std::runtime_error("Source actor '" + g.sourceActor + "' has no DNAFile");
        g.sourceDna = sourceRow->dna;
    }
    const data::Table* dna{};
    try { dna = dnaTable(catalog, customAssetPath(f, g.sourceDna)); }
    catch (const std::exception& error) {
        // Hand-written code DNA: keep the explicit historical contract.
        note(std::string("automatic defaults unavailable, source DNA is not plain data (") + error.what() + ")");
        g.expectedMask = g.sourceActor; g.rowSource = g.sourceActor; g.rowSourceDna = g.sourceDna; g.bodyDna = g.sourceDna;
        if (f.baseCharacter.empty()) throw std::runtime_error("BaseCharacter is required when the source DNA is not plain data");
        return;
    }
    g.sourceMask = text(*dna, {"AgentGadgets", "Identification", "ActorTypeMask"});
    const auto model = text(*dna, {"AgentGadgets", "EntityInstance", "ModelName"});
    const bool form = playerForm(*dna), bipedRig = biped(*dna);
    if (!form && !bipedRig && g.body == BodyMode::Auto)
        throw std::runtime_error("automatic conversion supports biped actors only; this source is neither a player avatar "
            "nor a biped NPC (set Body and Animations explicitly)");
    const auto body = g.body == BodyMode::Auto ? (form ? BodyMode::Source : BodyMode::Template) : g.body;
    if (body == BodyMode::Source && !form) note("Body=Source keeps a source DNA without player avatar components");
    if (!bipedRig && g.animations == AnimationMode::Auto) {
        g.animations = AnimationMode::Source;
        note("non-biped source keeps its own model and animations");
    }
    const auto tpl = loadTemplate(catalog, chooseTemplate(catalog, f, model, messages));
    if (f.baseCharacter.empty()) f.baseCharacter = tpl.gateway;

    // Inventory: the source's own player profile when complete, otherwise the
    // template's basic inventory without the template's personal tools.
    const auto tools = catalog.tools();
    const auto* sourceProfile = tools.profile(g.sourceMask.empty() ? g.sourceActor : g.sourceMask);
    const bool complete = inProfile(tools, sourceProfile, "ITEM_NONE") && inProfile(tools, sourceProfile, "PROP_NONE") &&
        inProfile(tools, sourceProfile, "ABILITY_NONE");
    if (g.inventorySource.empty() && bipedRig && (body == BodyMode::Template || !complete)) {
        g.inventorySource = tpl.mask;
        if (g.inventoryExclude.empty())
            for (const auto& personal : {tpl.start[0], tpl.start[2]})
                if (!personal.empty() && tools.find(personal) && personal != g.item && personal != g.ability) appendUnique(g.inventoryExclude, personal);
        note(std::string(sourceProfile ? "source tool profile is incomplete" : "source has no native tool profile") +
            "; using " + tpl.gateway + "'s basic inventory");
    }
    const auto* donorProfile = tools.profile(g.inventorySource);
    const auto available = [&](const std::string& id) {
        return std::find(g.inventoryExclude.begin(), g.inventoryExclude.end(), id) == g.inventoryExclude.end() &&
            (inProfile(tools, sourceProfile, id) || inProfile(tools, donorProfile, id));
    };
    // Starting equipment: the source's own usable tools, else the template's body-only prop.
    // Each empty slot is filled on its own, so an equipped custom item keeps the
    // template's body-only standard attack (for example PROP_BUZZ).
    g.exactLoadout = true;
    std::string automatic;
    for (unsigned i = 0; i < 3; ++i) {
        if (!slot(g, i).empty()) continue;
        const auto id = text(*dna, {"Components", "ToolSetManager", i == 0 ? "AutoStartTool1" : i == 1 ? "AutoStartTool2" : "AutoStartTool3"});
        const auto* row = id.empty() ? nullptr : tools.find(id);
        if (row && row->group == slotGroup(i) && (i != 0 || row->playerControls) && available(id)) slot(g, i) = id;
        if (i == 1 && g.prop.empty() && bipedRig) {
            const auto* prop = tpl.start[1].empty() ? nullptr : tools.find(tpl.start[1]);
            if (prop && prop->group == "Props" && prop->actors.empty() && available(prop->id)) g.prop = prop->id;
        }
        if (!slot(g, i).empty()) automatic += (automatic.empty() ? "" : ", ") + slot(g, i);
    }
    if (g.item.empty() && g.prop.empty() && g.ability.empty())
        throw std::runtime_error("no starting equipment could be inferred; set Item, Prop or Ability in [Character.Gameplay]");
    if (!automatic.empty()) note("starting equipment " + automatic + " chosen automatically");
    // A declared native tool joins the private inventory even when neither
    // profile lists it (for example a boss weapon on a converted NPC).
    for (unsigned i = 0; i < 3; ++i) {
        const auto& id = slot(g, i);
        const auto* row = id.empty() ? nullptr : tools.find(id);
        if (!row) continue;
        const bool listed = inProfile(tools, sourceProfile, id) || inProfile(tools, donorProfile, id);
        // Standalone [Tool] rows are already granted by their own mod.
        if (!listed && std::find(g.inventoryExclude.begin(), g.inventoryExclude.end(), id) == g.inventoryExclude.end() &&
            std::find(g.extraTools.begin(), g.extraTools.end(), id) == g.extraTools.end()) {
            g.extraTools.push_back(id);
            note(id + " added to the private inventory");
        }
    }
    // NPC-only items lack player control contexts (Allow_Gun...). Borrow them.
    if (!g.item.empty() && g.controlSources[0].empty())
        if (const auto* row = tools.find(g.item); row && row->group == "Items" && !row->playerControls) {
            g.controlSources[0] = defaultItemControl;
            note(g.item + " is NPC-only; player controls come from " + std::string(defaultItemControl) + " (set ItemControlSource to change)");
        }
    auto& weapon = g.itemWeapon;
    if (weapon.enabled() || weapon.radius || weapon.speed || weapon.lifetime || weapon.damage || weapon.scale) {
        const auto* row = tools.find(g.item);
        if (!row) throw std::runtime_error("Item tool missing from the native table: " + g.item);
        const bool needDna = weapon.dna.empty() || weapon.script.empty() || !weapon.enabled();
        if (needDna) {
            const auto inferred = inferToolSource(catalog, *row, f.assetAliases, !weapon.enabled());
            if (weapon.dna.empty()) weapon.dna = inferred.dna;
            if (weapon.script.empty()) weapon.script = inferred.script;
            if (!weapon.enabled()) weapon.source = inferred.weapon;
            if (weapon.script.empty()) throw std::runtime_error(g.item + " has no native script to clone; set ItemToolScript");
            note("weapon " + weapon.source + " cloned from " + weapon.dna + " / " + weapon.script);
        }
        for (const auto& effect : weaponEffects(catalog, weapon.source)) appendUnique(g.preloadFiles, effect);
    }
    // Every stock avatar row reaches the shared player set (punch and shoulder
    // charge effects) through its own include; NPC and cut rows do not.
    appendUnique(g.preloadIncludes, "biped_avatar");
    // Declared tools load with the private actor, wherever their own rows live.
    for (unsigned i = 0; i < 3; ++i)
        if (const auto* row = slot(g, i).empty() ? nullptr : tools.find(slot(g, i)))
            for (const auto& actor : row->actors) if (!actor.empty()) appendUnique(g.preloadActors, lower(actor));

    // Rows and body. A non-player source keeps only its visual identity on the
    // template's player DNA; a source without an Avatar row borrows the
    // template's actor/dependency rows with the model entries swapped.
    const auto templateRows = [&](const std::string& pieces) {
        g.rowSource = tpl.actor; g.rowSourceDna = tpl.dna; g.stripModelDef = true;
        if (!model.empty()) g.fileReplacements.emplace_back("entity|" + lower(tpl.model), "Entity|" + model);
        if (!tpl.pieces.empty()) g.fileReplacements.emplace_back("entity|" + lower(tpl.pieces), pieces.empty() ? "" : "Entity|" + pieces);
    };
    const auto pieces = text(*dna, {"AgentGadgets", "DynamicExploder", "dynexp_pieceFile"});
    std::string patch;
    if (body == BodyMode::Template) {
        g.bodyDna = tpl.dna; g.expectedMask = tpl.mask;
        templateRows(pieces);
        patch += "local g=DNA.AgentGadgets\nif type(g.EntityInstance)~='table' then g.EntityInstance={} end\n";
        if (const auto* entity = data::path(*dna, {"AgentGadgets", "EntityInstance"}); entity && entity->isTable())
            for (const auto* key : {"ModelName", "RootNode"}) {
                const auto* value = entity->table->field(key);
                patch += std::string("g.EntityInstance.") + key + "=" + (value ? data::literal(*value) : "nil") + "\n";
            }
        patch += "if type(g.Identification)=='table' then\n";
        for (const auto* key : {"Name", "DialogID", "LoadAudio"}) {
            const auto* value = data::path(*dna, {"AgentGadgets", "Identification", key});
            patch += std::string("g.Identification.") + key + "=" + (value ? data::literal(*value) : "nil") + "\n";
        }
        data::Value exploded; exploded.type = data::Value::Type::Table; exploded.table = std::make_shared<data::Table>();
        if (const auto* exploder = data::path(*dna, {"AgentGadgets", "DynamicExploder"}); exploder && exploder->isTable())
            for (const auto& [key, value] : exploder->table->fields)
                if (key.starts_with("dynexp_piece") || (key.starts_with("dynexp_control") && key.size() > 14 &&
                    std::all_of(key.begin() + 14, key.end(), [](char c) { return c >= '0' && c <= '9'; })))
                    exploded.table->fields[key] = value;
        patch += "end\nlocal e=g.DynamicExploder\nif type(e)=='table' then\n"
            "for key in pairs(e) do if string.find(key,'^dynexp_piece') or string.find(key,'^dynexp_control%d+$') then e[key]=nil end end\n"
            "for key,value in pairs(" + data::literal(exploded) + ") do e[key]=value end\nend\n";
        for (const auto* key : {"ActorScaleMedian", "ActorScaleDeviation"})
            if (const auto* value = dna->field(key)) patch += std::string("DNA.") + key + "=" + data::literal(*value) + "\n";
        note("non-player source: " + tpl.gateway + "'s player body with " + g.sourceDna + "'s model and identity");
    } else {
        g.bodyDna = g.sourceDna; g.expectedMask = g.sourceMask;
        // The avatar controller only drives PlayerControllable actor types. A
        // cut avatar's own type may be undefined (AV_Ferb): use the template's.
        const auto types = playerTypes(catalog);
        if (!types.empty() && !types.contains(lower(g.sourceMask))) {
            patch += "if type(DNA.AgentGadgets.Identification)=='table' then DNA.AgentGadgets.Identification.ActorTypeMask=" +
                mods::luaQuote(tpl.mask) + " end\n";
            note("ActorTypeMask '" + g.sourceMask + "' is not a player-controllable type; the private copy uses " + tpl.mask);
        }
        if (sourceRow && sourceRow->type == "Avatar" && lower(sourceRow->dna) == lower(g.sourceDna)) {
            g.rowSource = lower(g.sourceActor); g.rowSourceDna = sourceRow->dna;
        } else {
            if (!bipedRig) throw std::runtime_error("a non-biped source needs its own Avatar actor row");
            templateRows(pieces);
            note("source has no Avatar actor row; " + tpl.gateway + "'s actor and dependency rows are cloned");
        }
    }
    // Player model: generated only when no author model is mounted for it.
    if (!model.empty() && customAssetPath(f, model) == model) {
        const auto built = preparePlayerModel(catalog, assets, std::to_string(f.sku), model, tpl.model, g.animations, f.assetAliases);
        if (built.mode == AnimationMode::Template) {
            note("player entity built from " + tpl.gateway + "'s animation sets for " + model + " (" + std::to_string(built.files) + " files)");
            // The template's overrides describe the borrowed animation sets.
            if (body == BodyMode::Source)
                patch += "if type(DNA.AgentGadgets.AnimTree)=='table' then DNA.AgentGadgets.AnimTree.OverrideFile=" +
                    (tpl.overrides.empty() ? std::string("nil") : mods::luaQuote(tpl.overrides)) + " end\n";
        } else if (built.mode == AnimationMode::Complete) {
            std::string list;
            for (const auto& name : built.addedGroups) list += (list.empty() ? "" : ", ") + name;
            note("player entity completed with " + tpl.gateway + "'s " + list);
        }
    }
    g.bodyPatch = std::move(patch);
}
}
