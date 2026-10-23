// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_gameplay.hpp"
#include "di1/character_assets.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::string lower(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
    return value;
}
std::uint32_t word(std::string_view bytes, std::size_t at) {
    if (at > bytes.size() || bytes.size() - at < 4) throw std::runtime_error("truncated tool/DNA table");
    std::uint32_t value{};
    for (unsigned i = 0; i < 4; ++i) value |= std::uint32_t(static_cast<unsigned char>(bytes[at + i])) << (i * 8);
    return value;
}
std::string marker(const CustomFigure& f) { return "LiteralCrabCustomDNA:" + f.name; }
std::string toolMarker(const CustomFigure& f) { return "LiteralCrabCustomTool:" + f.name; }
void putWord(std::string& bytes,std::size_t at,std::uint32_t value) {
    if(at>bytes.size() || bytes.size()-at<4) throw std::runtime_error("truncated generated asset");
    for(unsigned i=0;i<4;++i) bytes[at+i]=static_cast<char>(value>>(i*8));
}
void addGenerated(mods::AssetIndex& assets,const std::string& path,std::string bytes) {
    if(assets.member(path)) throw std::runtime_error("private generated asset path already supplied by a mod: "+path);
    mods::AssetMember member;member.name=path;member.generated=std::make_shared<const std::string>(std::move(bytes));
    assets.members.push_back(std::move(member));
    std::sort(assets.members.begin(),assets.members.end(),[](const auto& a,const auto& b){return a.name<b.name;});
}
std::string originalAsset(mods::AssetIndex& assets, const std::wstring& root, const std::string& name) {
    if (const auto* member = assets.member(lower(name))) {
        std::string bytes;
        if (!assets.read(*member, bytes)) throw std::runtime_error("cannot read mod asset " + name);
        return bytes;
    }
    if (name == "var/toolset/toolset.bin") {
        mods::zip::Archive archive(root + L"/startup5.zip");
        const auto* entry = archive.find(name);
        if (!entry) throw std::runtime_error("original tool table missing");
        return archive.read(*entry);
    }
    auto slash = name.rfind('/');
    while (slash != std::string::npos) {
        const auto directory = name.substr(0, slash);
        const auto leaf = directory.substr(directory.rfind('/') + 1);
        const auto archiveName = directory + '/' + leaf + ".zip";
        try {
            mods::zip::Archive archive(root + L"/" + std::wstring(archiveName.begin(), archiveName.end()));
            if (const auto* entry = archive.find(name.substr(slash + 1))) return archive.read(*entry);
            if (const auto* entry = archive.find(name)) return archive.read(*entry);
        } catch (const std::exception&) { /* Try the containing archive. */ }
        if (!slash) break;
        slash = name.rfind('/', slash - 1);
    }
    throw std::runtime_error("source DNA not found: " + name);
}
std::string definitions(const std::vector<CustomFigure>& figures) {
    std::string code = "local figures={\n";
    for (const auto& f : figures) if (f.gameplay.enabled()) {
        code += "{sku=" + std::to_string(f.sku) + ",name=" + mods::luaQuote(f.name) + ",actor=" + mods::luaQuote(lower(f.actor)) +
            ",source=" + mods::luaQuote(lower(f.gameplay.sourceActor)) + ",sourceDna=" + mods::luaQuote(lower(f.gameplay.sourceDna)) +
            ",dna=" + mods::luaQuote(customGameplayDnaPath(f)) + ",kind='Avatar',actors={";
        for (const auto& actor : f.gameplay.preloadActors) code += mods::luaQuote(lower(actor)) + ",";
        const auto& w=f.gameplay.itemWeapon;
        if(w.enabled()) code+=mods::luaQuote(w.actor)+",";
        code += "},files={";
        for (const auto& file : f.gameplay.preloadFiles) code += mods::luaQuote(customAssetPath(f,file)) + ",";
        if(!f.gameplay.respawnBehavior.empty()) code+=mods::luaQuote("AITree|"+f.gameplay.respawnBehavior)+",";
        code += "},includes={";
        for (const auto& include : f.gameplay.preloadIncludes) code += mods::luaQuote(include) + ",";
        code += "},aliases={";
        for (const auto& [from,to] : f.assetAliases) code += "["+mods::luaQuote(from)+"]="+mods::luaQuote(to)+",";
        code += "}},\n";
        if(w.enabled()) {
            code+="{sku="+std::to_string(f.sku)+",name="+mods::luaQuote(f.name)+",actor="+mods::luaQuote(w.actor)+
            ",source="+mods::luaQuote(lower(w.sourceActor))+",sourceDna="+mods::luaQuote(lower(w.dna))+
            ",dna="+mods::luaQuote(w.privateDna)+",sourceScript="+mods::luaQuote(lower(w.script))+
            ",script="+mods::luaQuote(w.privateScript)+",kind='Other',actors={},files={},includes={},aliases={";
            for (const auto& [from,to] : f.assetAliases) code += "["+mods::luaQuote(from)+"]="+mods::luaQuote(to)+",";
            code+="}},\n";
        }
    }
    return code + "}\n";
}
}

std::string customGameplayDnaPath(const CustomFigure& f) {
    return "characters/literalcrab/" + lower(f.name) + ".dnax";
}
std::string identifyCustomDna(std::string_view bytes, std::string_view identity) {
    if (bytes.size() >= 4 && bytes.substr(0, 4) == std::string_view("\x1bLua", 4)) {
        if (bytes.size() < 16 || static_cast<unsigned char>(bytes[4]) != 0x51 || bytes[5] != 0 ||
            bytes[6] != 1 || bytes[7] != 4 || bytes[8] != 4 || bytes[9] != 4 || bytes[10] != 8 || bytes[11] != 0)
            throw std::runtime_error("unsupported source DNA bytecode format");
        const auto length = word(bytes, 12);
        if (length > bytes.size() - 16 || (length && bytes[15 + length] != '\0'))
            throw std::runtime_error("invalid source DNA chunk identity");
        std::string result(bytes.substr(0, 12));
        const auto newLength = static_cast<std::uint32_t>(identity.size() + 1);
        for (unsigned i = 0; i < 4; ++i) result += static_cast<char>(newLength >> (i * 8));
        result.append(identity); result += '\0'; result.append(bytes.substr(16 + length));
        return result;
    }
    if (bytes.empty() || bytes.find('\0') != std::string_view::npos) throw std::runtime_error("unsupported source DNA text");
    return "-- " + std::string(identity) + "\n" + std::string(bytes);
}

namespace {
std::vector<std::uint32_t> inventoryRows(std::string_view bytes, const CustomFigure& f) {
    std::array<std::string_view, 13> blocks;
    std::size_t at{};
    for (auto& block : blocks) {
        const auto size = word(bytes, at);
        if (size < 4 || size > bytes.size() - at) throw std::runtime_error("invalid tool table block");
        block = bytes.substr(at, size); at += size;
    }
    if (at != bytes.size() || (blocks[1].size() - 4) % 4) throw std::runtime_error("unsupported tool table layout");
    const auto strings = blocks[0].substr(4), values = blocks[1].substr(4);
    const auto text = [&](std::uint32_t index) -> std::string_view {
        if (index >= strings.size()) throw std::runtime_error("invalid tool string");
        const auto end = strings.find('\0', index);
        if (end == std::string_view::npos) throw std::runtime_error("unterminated tool string");
        return strings.substr(index, end - index);
    };
    const auto tools = word(blocks[11], 4), profiles = word(blocks[12], 4);
    if (tools > 65535 || profiles > 65535 || blocks[11].size() != 8ull + tools * 76ull ||
        blocks[12].size() != 8ull + profiles * 8ull) throw std::runtime_error("unsupported tool/profile rows");
    const auto profile = [&](const std::string& name) {
        std::vector<std::uint32_t> result;
        bool found{};
        for (std::uint32_t i = 0; i < profiles; ++i) {
            const auto row = 8 + i * 8;
            if (lower(std::string(text(word(blocks[12], row)))) != lower(name)) continue;
            if (found) throw std::runtime_error("ambiguous source tool profile: " + name);
            found = true;
            std::size_t offset = word(blocks[12], row + 4);
            if (offset >= values.size() / 4) throw std::runtime_error("invalid source inventory offset");
            for (;;) {
                if (offset >= values.size() / 4 || result.size() > tools) throw std::runtime_error("unterminated source inventory");
                const auto tool = word(values, offset++ * 4);
                if (tool == 0xffffffff) break;
                if (tool >= tools || std::find(result.begin(),result.end(),tool)!=result.end())
                    throw std::runtime_error("invalid or duplicate source inventory tool");
                result.push_back(tool);
            }
        }
        if (!found) throw std::runtime_error("native tool profile missing: " + name);
        return result;
    };
    auto inventory = profile(f.gameplay.sourceActor);
    if (!f.gameplay.inventorySource.empty()) {
        auto donor = profile(f.gameplay.inventorySource);
        std::vector<std::uint32_t> merged;
        for (const auto& excluded : f.gameplay.inventoryExclude) {
            const auto contains = [&](const auto& list) { return std::any_of(list.begin(),list.end(),[&](auto i) {
                return text(word(blocks[11],8+i*76)) == excluded; }); };
            if (!contains(inventory) && !contains(donor)) throw std::runtime_error("excluded inventory tool missing: " + excluded);
        }
        for (const auto* group : {"Items","Props","Abilities"}) for (const auto* list : {&inventory,&donor})
            for (const auto tool : *list) {
                if (text(word(blocks[11],8+tool*76+8)) != group) continue;
                const auto id = text(word(blocks[11],8+tool*76));
                if (std::find(f.gameplay.inventoryExclude.begin(),f.gameplay.inventoryExclude.end(),id)!=f.gameplay.inventoryExclude.end()) continue;
                if (std::find(merged.begin(),merged.end(),tool)==merged.end()) merged.push_back(tool);
            }
        if (merged.empty() || merged.size()>512) throw std::runtime_error("private inventory must contain 1..512 native tools");
        inventory = std::move(merged);
    }
    for (const auto& [requested, group] : std::array<std::pair<std::string, const char*>, 3>{
        {{f.gameplay.item, "Items"}, {f.gameplay.prop, "Props"}, {f.gameplay.ability, "Abilities"}}}) {
        if (requested.empty()) continue;
        bool accepted{};
        for (const auto tool : inventory) {
            const auto row = 8 + tool * 76;
            if (text(word(blocks[11], row)) == requested) {
                if (text(word(blocks[11], row + 8)) != group) throw std::runtime_error("wrong loadout group for " + requested);
                if (accepted) throw std::runtime_error("duplicate loadout tool definition");
                accepted = true;
            }
        }
        if (!accepted) throw std::runtime_error("loadout tool is absent from source avatar profile: " + requested);
    }
    return inventory;
}
}
void validateCustomLoadout(std::string_view bytes, const CustomFigure& f) {
    (void)inventoryRows(bytes,f);
}
void resolveCustomInventory(std::string_view bytes, CustomFigure& f) {
    const auto rows=inventoryRows(bytes,f);
    f.gameplay.inventoryRows.clear(); f.gameplay.inventoryHashes.clear();
    if(f.gameplay.inventorySource.empty()) return;
    std::size_t at{};
    for(unsigned i=0;i<11;++i) at+=word(bytes,at);
    for(const auto row:rows) f.gameplay.inventoryHashes.push_back(word(bytes,at+8+row*76+4));
    f.gameplay.inventoryRows=rows; f.gameplay.inventoryRows.push_back(~0u);
}

void prepareCustomCharacterGameplay(mods::AssetIndex& assets, const std::wstring& root,
    std::vector<CustomFigure>& figures, std::vector<std::string>& messages) {
    std::string tools;
    std::erase_if(figures, [&](CustomFigure& f) {
        if (!f.gameplay.enabled()) return false;
        try {
            if (tools.empty()) tools = originalAsset(assets, root, "var/toolset/toolset.bin");
            validateCustomLoadout(tools, f);
            resolveCustomInventory(tools, f);
            resolveCustomControlSources(tools, f);
            const auto sourceAsset = [&](const std::string& path) { return originalAsset(assets, root, customAssetPath(f,path)); };
            // The original tree stays shared by stock actors. Only a private
            // zero-piece avatar selects this copy in its DNA patch below.
            try {
                const auto behavior=sourceAsset("characters/avatar.btx");
                const auto path="characters/literalcrab/"+lower(f.name)+"_respawn.btx";
                addGenerated(assets,path,identifyCustomDna(behavior,"LiteralCrabRespawn:"+f.name));
                f.gameplay.respawnBehavior=path;
            } catch(const std::exception& error) {
                messages.push_back("Custom gameplay "+f.name+": private respawn tree unavailable: "+error.what());
            }
            auto& weapon=f.gameplay.itemWeapon;
            if(weapon.enabled()) {
                const auto prefix="lcr_"+std::to_string(f.sku)+"_item";
                weapon.actor=prefix;weapon.privateWeapon=prefix+"_weapon";
                weapon.privateDna="characters/literalcrab/"+prefix+".dnax";
                weapon.privateScript="characters/literalcrab/"+prefix+".sx";
                const auto script=remapSlngLiteral(sourceAsset(weapon.script),weapon.source,weapon.privateWeapon);
                const auto toolDna=identifyCustomDna(sourceAsset(weapon.dna),toolMarker(f));
                addGenerated(assets,weapon.privateScript,script);
                addGenerated(assets,weapon.privateDna,toolDna);
            }
            const auto path = customGameplayDnaPath(f);
            if (assets.member(path)) throw std::runtime_error("private generated DNA path already supplied by a mod");
            auto dna = identifyCustomDna(sourceAsset(f.gameplay.sourceDna), marker(f));
            mods::AssetMember member; member.name = path;
            member.generated = std::make_shared<const std::string>(std::move(dna));
            assets.members.push_back(std::move(member));
            std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
            messages.push_back("Custom gameplay " + f.name + ": native loadout validated; private source DNA prepared");
            return false;
        } catch (const std::exception& error) {
            messages.push_back("Custom gameplay " + f.name + ": REJECTED: " + error.what());
            return true;
        }
    });
}

void resolveCustomControlSources(std::string_view bytes, CustomFigure& f) {
    // validateCustomLoadout has checked the container. Keep this helper safe
    // independently too, for validation tools and mod authoring tests.
    validateCustomLoadout(bytes,f);
    std::array<std::string_view,13> blocks;
    std::size_t at{};
    for(auto& block:blocks) { const auto size=word(bytes,at);block=bytes.substr(at,size);at+=size; }
    const auto strings=blocks[0].substr(4);
    const auto text=[&](unsigned offset) {
        if(offset>=strings.size()) throw std::runtime_error("invalid control source string");
        const auto end=strings.find('\0',offset);
        if(end==std::string_view::npos) throw std::runtime_error("unterminated control source string");
        return strings.substr(offset,end-offset);
    };
    f.gameplay.controlRows={~0u,~0u};f.gameplay.controlHashes={};
    if(f.gameplay.itemWeapon.enabled()) {
        unsigned item=~0u;
        for(unsigned i=0;i<word(blocks[11],4);++i) if(text(word(blocks[11],8+i*76))==f.gameplay.item) item=i;
        if(item==~0u) throw std::runtime_error("weapon item definition missing");
        const auto valueCount=(blocks[1].size()-4)/4;
        const auto offset=word(blocks[11],8+item*76+0x18);
        if(offset>=valueCount || valueCount-offset<2 || word(blocks[1],4+(offset+1)*4)!=~0u)
            throw std::runtime_error("private weapon requires exactly one tool actor");
        const auto actor=word(blocks[1],4+offset*4),count=word(blocks[10],4);
        if(blocks[10].size()!=8ull+count*64ull || actor>=count) throw std::runtime_error("invalid tool actor definition");
        f.gameplay.itemWeapon.sourceActor=std::string(text(word(blocks[10],8+actor*64+8)));
    }
    for(unsigned slot=0;slot<2;++slot) {
        const auto& requested=f.gameplay.controlSources[slot];
        if(requested.empty()) continue;
        for(unsigned i=0;i<word(blocks[11],4);++i) {
            const auto row=8+i*76;
            if(text(word(blocks[11],row))!=requested) continue;
            if(f.gameplay.controlRows[slot]!=~0u) throw std::runtime_error("ambiguous control source tool");
            if(text(word(blocks[11],row+8))!=(slot==0?"Items":"Props")) throw std::runtime_error("wrong control source group");
            f.gameplay.controlRows[slot]=i;f.gameplay.controlHashes[slot]=word(blocks[11],row+4);
        }
        if(f.gameplay.controlRows[slot]==~0u) throw std::runtime_error("control source tool missing: "+requested);
    }
}

std::string customGameplayDnaPatch(const CustomFigure& f) {
    if (!f.gameplay.enabled()) return {};
    std::string code = "local _,_,log=...\nlocal expected=" + mods::luaQuote(lower(f.gameplay.sourceActor)) + "\nlocal loadout={";
    for (const auto& [key, value] : std::array<std::pair<const char*, std::string>, 3>{
        {{"AutoStartTool1", f.gameplay.item}, {"AutoStartTool2", f.gameplay.prop}, {"AutoStartTool3", f.gameplay.ability}}})
        if (!value.empty()) code += "[" + mods::luaQuote(key) + "]=" + mods::luaQuote(value) + ",";
    code += R"lua(}
if type(DNA)~='table' or type(DNA.AgentGadgets)~='table' or
   type(DNA.AgentGadgets.Identification)~='table' or
   string.lower(DNA.AgentGadgets.Identification.ActorTypeMask or '')~=expected or
   type(DNA.Components)~='table' or type(DNA.Components.ToolSetManager)~='table' then
    log('SKIPPED: custom loadout source DNA contract differs'); return
end
for key,value in pairs(loadout) do DNA.Components.ToolSetManager[key]=value end
-- The shared avatar death behavior waits for DynamicExploderReassembled.
-- NPC-derived avatars can omit this flag and remain cloaked forever after
-- combat death. The native zero-piece path also reassembles correctly.
-- Preserve authored values and apply this default only to the private DNA.
local exploder=DNA.AgentGadgets.DynamicExploder
if type(exploder)=='table' and exploder.dynexp_reassemble==nil then
    exploder.dynexp_reassemble=1
end
)lua";
    if(!f.gameplay.respawnBehavior.empty()) {
        code+="local ai=DNA.AgentGadgets.AiTree\n";
        code+="if type(exploder)=='table' and exploder.dynexp_reassemble==1 and "
            "(exploder.dynexp_pieceFile==nil or exploder.dynexp_pieceFile=='') and type(ai)=='table' and "
            "(string.lower(ai.BehaviorTreeFile or '')=='characters/avatar.btx' or "
            "string.lower(ai.BehaviorTreeFile or '')=="+mods::luaQuote(lower(customAssetPath(f,"characters/avatar.btx")))+") then\n";
        code+="ai.BehaviorTreeFile="+mods::luaQuote(f.gameplay.respawnBehavior)+"\nend\n";
    }
    if(f.gameplay.aimOffset) {
        const auto& v=*f.gameplay.aimOffset;
        code+="if type(DNA.AgentGadgets.TargetMode)=='table' then DNA.AgentGadgets.TargetMode.TargetingNormalOffset={"+
            std::to_string(v[0])+","+std::to_string(v[1])+","+std::to_string(v[2])+"} end\n";
    }
    return code + customAssetDnaPatch(f) + "DNA.Components.ToolSetManager.LiteralCrabFigure=" + mods::luaQuote(f.name) + R"lua(
log('APPLIED: custom native loadout on private avatar DNA')
)lua";
}

std::string customRespawnBehaviorPatch() {
    return R"lua(local _,_,log=...
if type(BehaviorTree)~='table' then return end
local death,prepare,ambiguous
local seen={}
local function visit(node)
    if type(node)~='table' or seen[node] then return end
    seen[node]=true
    if node.Name=='Death' and node.Type=='RoutingNode' then
        if death then ambiguous=true end
        death=node
    end
    if type(node.Data)=='table' and type(node.Data.Children)=='table' then
        for _,child in pairs(node.Data.Children) do visit(child) end
    end
end
visit(BehaviorTree)
if ambiguous or not death or type(death.Data)~='table' or
   type(death.Data.Router)~='table' or
   string.lower(death.Data.Router.RoutingScript or '')~='death.btl.sx' or
   type(death.Data.Children)~='table' then return end
for _,child in pairs(death.Data.Children) do
    if type(child)=='table' and child.Name=='PrepareForDeath' then
        if prepare then return end
        prepare=child
    end
end
if not prepare or prepare.Type~='LeafNode' or type(prepare.Data)~='table' or
   prepare.Data.BehaviorType~='SLEEP' or type(prepare.Data.HashOverrides)~='table' then return end
local hash=prepare.Data.HashOverrides
if hash.Duration~=0.1 then return end
-- Explode waits three native frames. A zero-piece avatar can reassemble
-- before the stock 0.1-second preparation finishes, losing the SweetDeath
-- interrupt and waiting for its ten-second fallback. Enter that leaf first.
hash.Duration=0.001
log('APPLIED: private zero-piece death waits for reassembly before its native event')
)lua";
}

std::string customGameplayActorPatch(const std::vector<CustomFigure>& figures) {
    return definitions(figures) + R"lua(
local _,_,log,environment=...
if type(Data)~='table' or type(Data.Records)~='table' then return end
if type(environment)~='table' then return end
local owners=environment.LiteralCrabPrivateGameplayRows
if type(owners)~='table' then owners=setmetatable({},{__mode='k'});environment.LiteralCrabPrivateGameplayRows=owners end
for _,figure in ipairs(figures) do
    local source,existing,conflict
    for _,row in ipairs(Data.Records) do
        if type(row)=='table' and type(row.Name)=='string' then
            local name=string.lower(row.Name)
            if name==figure.source then if source then conflict=true end; source=row end
            if name==figure.actor then if existing then conflict=true end; existing=row end
        end
    end
    if existing and (existing.Type~=figure.kind or owners[existing]~=figure.sku) then conflict=true end
    if conflict then
        log('SKIPPED: ambiguous private gameplay actor '..figure.actor)
        if LiteralCrab and LiteralCrab.DisableCustomFigure then LiteralCrab.DisableCustomFigure(figure.sku) end
    elseif source and source.Type==figure.kind and type(source.Parms)=='string' then
        local matches=0
        local parms=string.gsub(source.Parms,'DNAFile=([^;]+)',function(path)
            if string.lower(path)==figure.sourceDna then matches=matches+1; return 'DNAFile='..figure.dna end
            return 'DNAFile='..path
        end)
        if matches==1 and not existing then
            local row={}; for key,value in pairs(source) do row[key]=value end
            row.Name,row.Parms=figure.actor,parms
            owners[row]=figure.sku
            Data.Records[#Data.Records+1]=row
            log('APPLIED: private gameplay actor '..figure.actor)
        elseif existing and (existing.Parms~=parms or existing.Type~=figure.kind or owners[existing]~=figure.sku) then
            log('SKIPPED: private gameplay actor conflicts '..figure.actor)
            if LiteralCrab and LiteralCrab.DisableCustomFigure then LiteralCrab.DisableCustomFigure(figure.sku) end
        end
    end
end
)lua";
}

std::string customGameplayDependenciesPatch(const std::vector<CustomFigure>& figures) {
    return definitions(figures) + R"lua(
local _,_,log,environment=...
if type(DataMap)~='table' or type(DataMap.Actor)~='table' then return end
if type(environment)~='table' then return end
local owners=environment.LiteralCrabPrivateGameplayRows
if type(owners)~='table' then owners=setmetatable({},{__mode='k'});environment.LiteralCrabPrivateGameplayRows=owners end
local function appendUnique(list,value)
    for _,old in ipairs(list) do if string.lower(old)==string.lower(value) then return end end
    list[#list+1]=value
end
for _,figure in ipairs(figures) do
    local source=DataMap.Actor[figure.source]
    local existing
    for name,row in pairs(DataMap.Actor) do
        if type(name)=='string' and string.lower(name)==figure.actor then existing=row end
    end
    if existing and (type(existing)~='table' or owners[existing]~=figure.sku) then
        log('SKIPPED: private gameplay dependency conflict '..figure.actor)
        if LiteralCrab and LiteralCrab.DisableCustomFigure then LiteralCrab.DisableCustomFigure(figure.sku) end
    end
    if type(source)=='table' and type(source.Files)=='table' and existing==nil then
        local row,files,includes={},{},{}
        local found=false
        for key,value in pairs(source) do row[key]=value end
        for _,file in ipairs(source.Files) do
            if string.lower(file)=='dna|'..figure.sourceDna then file='DNA|'..figure.dna; found=true end
            if figure.sourceScript and string.lower(file)=='script|'..figure.sourceScript then file='Script|'..figure.script end
            local kind,path=string.match(file,'^([^|]+|)(.*)$')
            if kind and figure.aliases[string.lower(path)] then file=kind..figure.aliases[string.lower(path)] end
            files[#files+1]=file
        end
        if found then
            if type(source.Includes)=='table' then for _,value in ipairs(source.Includes) do includes[#includes+1]=value end end
            for _,actor in ipairs(figure.actors) do appendUnique(includes,actor..'|Actor') end
            for _,include in ipairs(figure.includes) do appendUnique(includes,include..'|Include') end
            for _,file in ipairs(figure.files) do appendUnique(files,file) end
            row.Files,row.Includes=files,includes
            owners[row]=figure.sku
            DataMap.Actor[figure.actor]=row
            log('APPLIED: private gameplay dependencies '..figure.actor)
        end
    end
end
)lua";
}

std::string remapSlngLiteral(std::string_view bytes,std::string_view source,std::string_view replacement) {
    if(bytes.size()<0x68 || bytes.substr(0,12)!=std::string_view("SLNG\x0c\x10\0\0\x78\x56\x34\x12",12) ||
        word(bytes,12)!=bytes.size()) throw std::runtime_error("unsupported native tool script");
    const auto count=word(bytes,0x30);
    const auto offset=word(bytes,0x64);
    const auto list=0x65ull+offset;
    if(!offset || count>65535 || list>bytes.size() || count*4ull>bytes.size()-list)
        throw std::runtime_error("invalid native script literal table");
    std::string result(bytes);unsigned replaced{};
    for(unsigned i=0;i<count;++i) {
        const auto entry=static_cast<std::size_t>(list+i*4);
        const auto relative=word(bytes,entry);
        if(!relative) continue;
        const auto start=entry+static_cast<std::uint64_t>(relative)+1;
        if(start>=bytes.size()) throw std::runtime_error("invalid native script literal");
        const auto end=bytes.find('\0',static_cast<std::size_t>(start));
        if(end==std::string_view::npos) throw std::runtime_error("unterminated native script literal");
        if(bytes.substr(static_cast<std::size_t>(start),end-static_cast<std::size_t>(start))!=source) continue;
        if(result.size()>0xffffffffull-replacement.size()-1) throw std::runtime_error("native script too large");
        putWord(result,entry,static_cast<unsigned>(result.size()-entry-1));
        result.append(replacement);result+='\0';++replaced;
    }
    if(!replaced) throw std::runtime_error("weapon name not referenced by native tool script");
    putWord(result,12,static_cast<unsigned>(result.size()));
    return result;
}

std::string customWeaponDefinitionsPatch(const std::vector<CustomFigure>& figures) {
    std::string code="local _,_,log,environment=...\nlocal definitions={\n";
    for(const auto& f:figures) {
        const auto& w=f.gameplay.itemWeapon;if(!w.enabled()) continue;
        code+="{sku="+std::to_string(f.sku)+",source="+mods::luaQuote(w.source)+",name="+mods::luaQuote(w.privateWeapon)+",values={";
        for(const auto& [key,value]:std::array<std::pair<const char*,std::optional<double>>,5>{
            {{"collisionRadius",w.radius},{"speed",w.speed},{"lifetime",w.lifetime},{"damage",w.damage},{"scale",w.scale}}})
            if(value) code+=std::string(key)+"="+std::to_string(*value)+",";
        code+="}},\n";
    }
    return code+R"lua(}
if type(weapon)~='table' or type(proj)~='table' then return end
if type(environment)~='table' then return end
local owners=environment.LiteralCrabPrivateGameplayRows
if type(owners)~='table' then owners=setmetatable({},{__mode='k'});environment.LiteralCrabPrivateGameplayRows=owners end
local function find(rows,name)
    local found
    for _,row in ipairs(rows) do
        if type(row)=='table' and type(row.name)=='string' and string.lower(row.name)==string.lower(name or '') then
            if found then return nil,true end
            found=row
        end
    end
    return found
end
for _,definition in ipairs(definitions) do
    local source,sourceConflict=find(weapon,definition.source)
    local projectile,projectileConflict
    if source then projectile,projectileConflict=find(proj,source.projectile) end
    local existingGun,gunConflict=find(weapon,definition.name)
    local existingShot,shotConflict=find(proj,definition.name)
    local conflict=sourceConflict or projectileConflict or gunConflict or shotConflict or
       ((existingGun or existingShot) and not (existingGun and existingShot and
       owners[existingGun]==definition.sku and owners[existingShot]==definition.sku)) or
       not source or not projectile or source.class~='gun' or projectile.class~='widget'
    -- A private name owned by another definition must never become the weapon
    -- resolved by the generated script. Keep its figure unavailable.
    if conflict then
        log('SKIPPED: private weapon/projectile conflict '..definition.name)
        if LiteralCrab and LiteralCrab.DisableCustomFigure then LiteralCrab.DisableCustomFigure(definition.sku) end
    end
    if not conflict and source and source.class=='gun' and projectile and projectile.class=='widget' and
       not find(weapon,definition.name) and not find(proj,definition.name) then
        local gun,shot={},{}
        for k,v in pairs(source) do gun[k]=v end
        for k,v in pairs(projectile) do shot[k]=v end
        gun.name,gun.projectile=definition.name,definition.name
        shot.name=definition.name
        owners[gun],owners[shot]=definition.sku,definition.sku
        for k,v in pairs(definition.values) do
            if k=='speed' then shot.launchSpeed={0,0,v} else shot[k]=v end
        end
        weapon[#weapon+1]=gun;proj[#proj+1]=shot
        log('APPLIED: private weapon/projectile '..definition.name)
    end
end
)lua";
}

void addCustomGameplayPatches(mods::Index& index, const std::vector<CustomFigure>& figures) {
    bool any{};
    for (const auto& f : figures) if (f.gameplay.enabled()) {
        any = true;
        const auto path = customGameplayDnaPath(f), identity = marker(f), content = "contains:" + identity;
        // The engine reports the source identity embedded in compiled DNA instead of its
        // asset path. Content targets are only scanned for unnamed chunks.
        index.addBuiltinPatch({path, identity, content}, "custom_loadout_" + f.name, customGameplayDnaPatch(f), "{}");
        if(!f.gameplay.respawnBehavior.empty()) {
            const auto id="LiteralCrabRespawn:"+f.name;
            index.addBuiltinPatch({f.gameplay.respawnBehavior,id,"contains:"+id},
                "custom_respawn_"+f.name,customRespawnBehaviorPatch(),"{}");
        }
        if(f.gameplay.itemWeapon.enabled()) {
            const auto& w=f.gameplay.itemWeapon;
            const auto toolIdentity=toolMarker(f), toolContent="contains:"+toolIdentity;
            auto patch="local _,_,log=...\nif type(DNA)~='table' or type(DNA.AgentGadgets)~='table' or type(DNA.AgentGadgets.Script)~='table' or "
                "string.lower(DNA.AgentGadgets.Script.ScriptName or '')~="+mods::luaQuote(lower(w.script))+" then log('SKIPPED: private tool DNA differs');return end\n";
            // A mod-local source DNA may already have had its resource aliases remapped.
            patch+="DNA.AgentGadgets.Script.ScriptName="+mods::luaQuote(w.privateScript)+"\n"+customAssetDnaPatch(f)+"\nlog('APPLIED: private tool script')\n";
            index.addBuiltinPatch({w.privateDna,toolIdentity,toolContent},"custom_weapon_"+f.name,patch,"{}");
        }
    }
    if (!any) return;
    index.addBuiltinPatch({"weapons.lua","var/weapons/weapons.lua","contains:projectileChoName"},
        "custom_weapon_definitions",customWeaponDefinitionsPatch(figures),"{}");
    index.addBuiltinPatch({"common_actors.lua", "rumpusroom_actors.lua", "toystoryinspace_actors.lua", "contains:ActorList"},
        "custom_gameplay_actors", customGameplayActorPatch(figures), "{}");
    index.addBuiltinPatch({"coredatamap.lua", "rumpusroomdatamap.lua", "toystoryinspacedatamap.lua", "contains:DataMap =", "contains:AnimTreeProject"},
        "custom_gameplay_dependencies", customGameplayDependenciesPatch(figures), "{}");
}
}
