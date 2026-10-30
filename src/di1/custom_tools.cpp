// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_tools.hpp"
#include "di1/character_gameplay.hpp"
#include "di1/character_profile.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::string lower(std::string_view text) {
    std::string result(text);
    for(auto& c:result) { if(c>='A'&&c<='Z') c+=32; if(c=='\\') c='/'; }
    return result;
}
bool identifier(std::string_view value) {
    return !value.empty() && value.size()<=96 && std::all_of(value.begin(),value.end(),[](unsigned char c) {
        return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';
    });
}
bool path(std::string_view value) {
    return !value.empty() && value.size()<180 && value.front()!='/' && value.back()!='/' &&
        value.find("..") == value.npos && value.find("//")==value.npos &&
        std::all_of(value.begin(),value.end(),[](unsigned char c) {
            return (c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'||c=='/';
        });
}
bool printable(std::string_view value,std::size_t limit) {
    return value.size()<=limit && std::all_of(value.begin(),value.end(),[](unsigned char c){return c>=32&&c<=126;});
}
std::string ascii(std::wstring_view value) {
    std::string result;
    for(auto c:value) { if(c<32||c>126) throw std::runtime_error("Tool fields must be printable ASCII"); result+=static_cast<char>(c); }
    return result;
}
using Fields=std::map<std::string,std::string>;
Fields section(const std::wstring& file) {
    std::array<wchar_t,32768> buffer{};
    const auto count=GetPrivateProfileSectionW(L"Tool",buffer.data(),static_cast<DWORD>(buffer.size()),file.c_str());
    if(count>=buffer.size()-2) throw std::runtime_error("Tool section exceeds 32 KiB");
    Fields fields;
    for(const wchar_t* entry=buffer.data();*entry;entry+=std::wcslen(entry)+1) {
        std::wstring_view line(entry);if(line.front()==L';'||line.front()==L'#') continue;
        const auto split=line.find(L'=');if(split==line.npos) throw std::runtime_error("invalid Tool entry");
        auto key=line.substr(0,split),value=line.substr(split+1);
        while(!key.empty()&&key.back()==L' ') key.remove_suffix(1);
        while(!value.empty()&&value.front()==L' ') value.remove_prefix(1);
        while(!value.empty()&&value.back()==L' ') value.remove_suffix(1);
        if(value.size()>=2&&((value.front()==L'"'&&value.back()==L'"')||(value.front()==L'\''&&value.back()==L'\''))) {
            value.remove_prefix(1);value.remove_suffix(1);
        }
        if(!fields.emplace(lower(ascii(key)),ascii(value)).second) throw std::runtime_error("duplicate Tool key");
    }
    if(!fields.empty()) {
        std::ifstream input(std::filesystem::path(file),std::ios::binary);
        const std::string source((std::istreambuf_iterator<char>(input)),{});
        if(!input||source.find('\0')!=source.npos) throw std::runtime_error("Tool manifest must use plain ASCII/UTF-8 text");
        unsigned sections{};std::size_t start{};
        while(start<source.size()) {
            const auto end=source.find('\n',start);auto line=std::string_view(source).substr(start,end==source.npos?source.size()-start:end-start);
            while(!line.empty()&&(line.front()==' '||line.front()=='\t')) line.remove_prefix(1);
            const auto close=line.find(']');
            if(close!=line.npos&&lower(line.substr(0,close+1))=="[tool]") ++sections;
            if(end==source.npos) break;start=end+1;
        }
        if(sections!=1) throw std::runtime_error("exactly one Tool section per mod is supported");
    }
    return fields;
}
std::string get(const Fields& fields,const char* key) { const auto i=fields.find(key);return i==fields.end()?std::string{}:i->second; }
std::vector<std::string> list(std::string text) {
    std::vector<std::string> values;
    while(!text.empty()) {
        const auto comma=text.find(',');auto value=text.substr(0,comma);
        while(!value.empty()&&value.front()==' ') value.erase(value.begin());
        while(!value.empty()&&value.back()==' ') value.pop_back();
        if(value.empty()||std::any_of(values.begin(),values.end(),[&](const auto& old){return lower(old)==lower(value);}))
            throw std::runtime_error("empty or duplicate Tool list entry");
        values.push_back(value);if(values.size()>64) throw std::runtime_error("at most 64 Tool list entries");
        if(comma==text.npos) break;
        text.erase(0,comma+1);if(text.empty()) throw std::runtime_error("empty Tool list entry");
    }
    return values;
}
std::uint32_t word(std::string_view bytes,std::size_t at) {
    if(at>bytes.size()||bytes.size()-at<4) throw std::runtime_error("truncated custom tool table");
    std::uint32_t v{};for(unsigned i=0;i<4;++i) v|=std::uint32_t(static_cast<unsigned char>(bytes[at+i]))<<(8*i);return v;
}
struct ToolInfo { std::uint32_t row{},hash{};std::string group; };
ToolInfo info(std::string_view bytes,std::string_view id,std::string_view sourceActor) {
    std::array<std::string_view,13> blocks;std::size_t at{};
    for(auto& b:blocks) {const auto size=word(bytes,at);if(size<4||size>bytes.size()-at) throw std::runtime_error("invalid tool table block");b=bytes.substr(at,size);at+=size;}
    if(at!=bytes.size()) throw std::runtime_error("unexpected tool table tail");
    const auto text=[&](unsigned offset) {
        const auto strings=blocks[0].substr(4);if(offset>=strings.size()) throw std::runtime_error("invalid tool string");
        const auto end=strings.find('\0',offset);if(end==strings.npos) throw std::runtime_error("unterminated tool string");return strings.substr(offset,end-offset);
    };
    const auto count=word(blocks[11],4);if(blocks[11].size()!=8ull+count*76ull) throw std::runtime_error("invalid tool rows");
    const auto actors=word(blocks[10],4);
    if(blocks[10].size()!=8ull+actors*64ull) throw std::runtime_error("invalid tool actor rows");
    bool actorFound{};
    for(unsigned i=0;i<actors;++i) if(lower(text(word(blocks[10],16+i*64)))==lower(sourceActor)) actorFound=true;
    if(!actorFound) throw std::runtime_error("SourceActor is not an existing tool actor: "+std::string(sourceActor));
    std::optional<ToolInfo> result;
    for(unsigned i=0;i<count;++i) if(text(word(blocks[11],8+i*76))==id) {
        if(result) throw std::runtime_error("ambiguous tool ID");
        result=ToolInfo{i,word(blocks[11],12+i*76),std::string(text(word(blocks[11],16+i*76)))};
    }
    if(!result) throw std::runtime_error("base/custom tool missing: "+std::string(id));return *result;
}
std::string readAsset(const mods::AssetIndex& assets,const std::wstring& root,std::string name) {
    name=lower(name);
    if(const auto* member=assets.member(name)) {
        std::string bytes;if(!assets.read(*member,bytes)) throw std::runtime_error("cannot read tool asset: "+name);return bytes;
    }
    if(name=="var/toolset/toolset.bin") {
        mods::zip::Archive archive(root+L"/startup5.zip");const auto* entry=archive.find(name);
        if(!entry) throw std::runtime_error("original tool table missing");return archive.read(*entry);
    }
    auto slash=name.rfind('/');
    while(slash!=name.npos) {
        const auto directory=name.substr(0,slash),leaf=directory.substr(directory.rfind('/')+1);
        const auto archivePath=directory+'/'+leaf+".zip";
        try {
            mods::zip::Archive archive(root+L"/"+std::wstring(archivePath.begin(),archivePath.end()));
            if(const auto* entry=archive.find(name.substr(slash+1))) return archive.read(*entry);
            if(const auto* entry=archive.find(name)) return archive.read(*entry);
        } catch(const std::exception&) {}
        if(!slash) break;slash=name.rfind('/',slash-1);
    }
    throw std::runtime_error("tool source asset missing: "+name);
}
void generated(mods::AssetIndex& assets,const std::string& name,std::string bytes,bool replace=false) {
    for(auto& m:assets.members) if(m.name==name) {
        if(!replace) throw std::runtime_error("generated tool resource already claimed: "+name);
        m.generated=std::make_shared<const std::string>(std::move(bytes));return;
    }
    mods::AssetMember m;m.name=name;m.generated=std::make_shared<const std::string>(std::move(bytes));assets.members.push_back(std::move(m));
    std::sort(assets.members.begin(),assets.members.end(),[](const auto& a,const auto& b){return a.name<b.name;});
}
std::string marker(const CustomTool& tool) { return "LiteralCrabTool:"+tool.table.id; }
// Mod-relative author Lua; bytecode and links are refused like other mod scripts.
std::string toolFile(const std::wstring& mod,const std::string& relative,std::size_t limit) {
    if(!path(relative)||!lower(relative).ends_with(".lua")||relative.front()=='.') throw std::runtime_error("Tool DNA/DNAScript must be a relative .lua file: "+relative);
    const auto file=std::filesystem::path(mod)/std::filesystem::path(relative);
    for(auto p=file;p!=std::filesystem::path(mod)&&p.has_parent_path();p=p.parent_path()) {
        const auto flags=GetFileAttributesW(p.c_str());
        if(flags==INVALID_FILE_ATTRIBUTES||(flags&FILE_ATTRIBUTE_REPARSE_POINT)) throw std::runtime_error("Tool Lua file missing or behind a link: "+relative);
    }
    std::ifstream input(file,std::ios::binary);
    std::string bytes((std::istreambuf_iterator<char>(input)),{});
    if(!input.eof()&&!input) throw std::runtime_error("cannot read Tool Lua file: "+relative);
    if(bytes.size()>limit||bytes.starts_with("\x1bLua")) throw std::runtime_error("Tool Lua file is too large or bytecode: "+relative);
    return bytes;
}
// Fills BaseTool, SourceActor/SourceDNA/SourceScript and WeaponSource from
// the native Source row. A player tool is its own base; an NPC-only item
// (no player control contexts) uses the default player item for its controls.
void resolveCustomTool(NativeCatalog& catalog,const ToolTableInfo& table,CustomTool& t,std::vector<std::string>& messages) {
    const auto note=[&](const std::string& text){messages.push_back("Custom tool "+t.table.id+": "+text);};
    const auto& sourceId=t.source.empty()?t.table.baseTool:t.source;
    const auto* source=table.find(sourceId);
    if(!source) throw std::runtime_error("Source tool missing from the native table: "+sourceId);
    if(t.table.baseTool.empty()) {
        if(source->group=="Items"&&!source->playerControls) {
            t.table.baseTool=defaultItemControl;
            note(sourceId+" is NPC-only; BaseTool "+t.table.baseTool+" supplies player controls and rules (set BaseTool to change)");
        } else t.table.baseTool=sourceId;
    }
    const bool projectile=t.radius||t.scale||t.speed||t.lifetime||t.damage;
    const bool wantWeapon=t.weaponSource.empty()&&projectile;
    if(t.sourceActor.empty()||wantWeapon||((projectile||!t.weaponSource.empty())&&t.sourceScript.empty())) {
        const auto inferred=inferToolSource(catalog,*source,t.assetAliases,wantWeapon);
        if(t.sourceActor.empty()) {t.sourceActor=inferred.actor;t.sourceDna=inferred.dna;}
        // A private script copy is only needed when its gun is cloned.
        if(t.sourceScript.empty()&&(projectile||!t.weaponSource.empty())) t.sourceScript=inferred.script;
        if(wantWeapon) t.weaponSource=inferred.weapon;
        note("copies "+t.sourceActor+" ("+t.sourceDna+(t.sourceScript.empty()?"":", "+t.sourceScript)+(t.weaponSource.empty()?"":", gun "+t.weaponSource)+")");
    }
    if(!t.weaponSource.empty()&&t.sourceScript.empty()) throw std::runtime_error("WeaponSource needs the tool's native SourceScript");
    if(!t.weaponSource.empty()) for(const auto& effect:weaponEffects(catalog,t.weaponSource))
        if(std::none_of(t.preloadFiles.begin(),t.preloadFiles.end(),[&](const auto& f){return lower(f)==lower(effect);})&&t.preloadFiles.size()<64) t.preloadFiles.push_back(effect);
}
std::string declarations(const std::vector<CustomTool>& tools) {
    std::string code="local tools={\n";
    for(const auto& t:tools) {
        code+="{id="+mods::luaQuote(t.table.id)+",actor="+mods::luaQuote(t.table.actor)+",source="+mods::luaQuote(lower(t.sourceActor))+
            ",sourceDna="+mods::luaQuote(lower(t.sourceDna))+",dna="+mods::luaQuote(t.privateDna)+",script="+mods::luaQuote(t.privateScript)+
            ",sourceScript="+mods::luaQuote(lower(t.sourceScript))+",model="+mods::luaQuote(privateAssetPath(t.assetAliases,t.model))+
            ",weapon="+mods::luaQuote(t.privateWeapon)+",weaponSource="+mods::luaQuote(t.weaponSource)+",targets={";
        for(auto sku:t.targetSkus) code+=std::to_string(sku)+",";
        code+="},actors={";for(const auto& a:t.preloadActors) code+=mods::luaQuote(lower(a))+",";
        code+="},includes={";for(const auto& a:t.preloadIncludes) code+=mods::luaQuote(a)+",";
        code+="},files={";for(const auto& a:t.preloadFiles) code+=mods::luaQuote(privateAssetPath(t.assetAliases,a))+",";
        code+="},aliases={";for(const auto& [a,b]:t.assetAliases) code+="["+mods::luaQuote(a)+"]="+mods::luaQuote(b)+",";
        code+="},values={";
        for(const auto& [key,value]:std::array<std::pair<const char*,std::optional<double>>,5>{
            {{"collisionRadius",t.radius},{"scale",t.scale},{"speed",t.speed},{"lifetime",t.lifetime},{"damage",t.damage}}})
            if(value) code+=std::string(key)+"="+std::to_string(*value)+",";
        code+="}},\n";
    }
    return code+"}\n";
}
constexpr auto helpers=R"lua(
local _,_,log,environment=...
if type(environment)~='table' then return end
local owners=environment.LiteralCrabCustomToolRows
if type(owners)~='table' then owners=setmetatable({},{__mode='k'});environment.LiteralCrabCustomToolRows=owners end
local function blocked(t,reason)
    log('SKIPPED: custom tool '..t.id..': '..reason)
    if LiteralCrab and LiteralCrab.DisableCustomFigure then
        for _,sku in ipairs(t.targets) do LiteralCrab.DisableCustomFigure(sku) end
    end
end
local function append(list,value)
    for _,old in ipairs(list) do if string.lower(old)==string.lower(value) then return end end
    list[#list+1]=value
end
local function copy(value,seen)
    if type(value)~='table' then return value end
    seen=seen or {};if seen[value] then return seen[value] end
    local result={};seen[value]=result
    for k,v in pairs(value) do result[k]=copy(v,seen) end
    return result
end
local function remap(t,value)
    local prefix,path=string.match(value,'^([^|]+|)(.*)$')
    if not prefix then prefix,path='',value end
    local key=string.lower((string.gsub(path,'\\','/')))
    return prefix..(t.aliases[key] or path)
end
local function remapTree(t,value,seen)
    if type(value)~='table' then return end
    seen=seen or {};if seen[value] then return end;seen[value]=true
    for k,v in pairs(value) do
        if type(v)=='table' then remapTree(t,v,seen)
        elseif type(v)=='string' and string.find(v,'/',1,true) then value[k]=remap(t,v) end
    end
end
)lua";
struct Label { std::string key,text; };
std::vector<Label> labels;
}

std::vector<CustomTool> loadCustomTools(const std::vector<mods::Mod>& mods,std::vector<std::string>& messages) {
    std::vector<CustomTool> tools;std::set<std::string> ids;
    for(const auto& mod:mods) try {
        const auto fields=section(mod.path+L"/mod.ini");if(fields.empty()) continue;
        constexpr const char* known[]={"id","basetool","controlsource","characters","displayname","description","sourceactor","sourcedna","sourcescript",
            "model","icon","assetdirectory","weaponsource","projectileradius","projectilescale","projectilespeed","projectilelifetime","projectiledamage",
            "preloadactors","preloadfiles","preloadincludes","equip","modelhandle","source","dna","dnascript","baserules"};
        for(const auto& [key,value]:fields) if(std::none_of(std::begin(known),std::end(known),[&](auto k){return key==k;}))
            throw std::runtime_error("unknown Tool key: "+key);
        if(tools.size()>=64) throw std::runtime_error("at most 64 custom tools");
        CustomTool t;t.table.id=get(fields,"id");t.table.baseTool=get(fields,"basetool");t.table.controlSource=get(fields,"controlsource");
        if(!identifier(t.table.id)||t.table.id.size()<=4||t.table.id.size()>64||!lower(t.table.id).starts_with("lcr_")) throw std::runtime_error("Tool Id must be a unique LCR_ identifier of 5..64 bytes");
        if(ids.contains(lower(t.table.id))) throw std::runtime_error("duplicate custom tool Id");
        t.source=get(fields,"source");
        // Source alone is enough: BaseTool, actor, DNA, script and gun are inferred at startup.
        if(t.source.empty()&&t.table.baseTool.empty()) throw std::runtime_error("Tool needs Source (a native tool ID to copy) or BaseTool");
        if((!t.source.empty()&&!identifier(t.source))||(!t.table.baseTool.empty()&&!identifier(t.table.baseTool))||
            (!t.table.controlSource.empty()&&!identifier(t.table.controlSource))) throw std::runtime_error("invalid source/base/control tool ID");
        t.table.modelHandle=get(fields,"modelhandle");
        if(!t.table.modelHandle.empty()&&!identifier(t.table.modelHandle)) throw std::runtime_error("ModelHandle must name a native model control");
        t.table.actor="lcr_tool_"+lower(t.table.id);t.modPath=mod.path;
        t.sourceActor=get(fields,"sourceactor");t.sourceDna=get(fields,"sourcedna");t.sourceScript=get(fields,"sourcescript");
        if((!t.sourceActor.empty()&&(!identifier(t.sourceActor)||lower(t.sourceActor)==t.table.actor))||
            (!t.sourceDna.empty()&&(!path(t.sourceDna)||!lower(t.sourceDna).ends_with(".dnax")))||(t.sourceActor.empty()!=t.sourceDna.empty()))
            throw std::runtime_error("SourceActor and SourceDNA go together (or omit both to infer them from Source)");
        if(!t.sourceScript.empty()&&(!path(t.sourceScript)||!lower(t.sourceScript).ends_with(".sx"))) throw std::runtime_error("SourceScript must name a relative .sx resource");
        t.characters=list(get(fields,"characters"));
        if(t.characters.empty()||std::any_of(t.characters.begin(),t.characters.end(),[](const auto& s){return s!="*"&&!identifier(s);})||
            (t.characters.size()!=1&&std::find(t.characters.begin(),t.characters.end(),"*")!=t.characters.end())) throw std::runtime_error("Characters must name custom figures, or only *");
        const auto equip=get(fields,"equip");
        if(!equip.empty()&&equip!="0"&&equip!="1") throw std::runtime_error("Equip must be 0 or 1");
        t.equip=equip=="1";
        // A granted tool belongs to its characters, like their own ITEM_<name>:
        // the base tool's purchase (UP_...) and Toy Box worlds would otherwise
        // leave it locked and uncreated in play sets. BaseRules=1 keeps them.
        const auto rules=get(fields,"baserules");
        if(!rules.empty()&&rules!="0"&&rules!="1") throw std::runtime_error("BaseRules must be 0 or 1");
        t.table.personal=rules!="1";
        t.model=get(fields,"model");t.icon=get(fields,"icon");t.assetDirectory=get(fields,"assetdirectory");
        if((!t.model.empty()&&!path(t.model))||(!t.icon.empty()&&(!path(t.icon)||!lower(t.icon).starts_with("ui/")))) throw std::runtime_error("invalid Model/Icon resource path");
        if(!t.assetDirectory.empty()&&(!path(t.assetDirectory)||t.assetDirectory.front()=='.'||lower(t.assetDirectory)=="assets"||lower(t.assetDirectory).starts_with("assets/")))
            throw std::runtime_error("AssetDirectory must be a relative private mod folder");
        t.displayName=get(fields,"displayname");if(t.displayName.empty()) t.displayName=t.table.id;
        t.description=get(fields,"description");if(!printable(t.displayName,255)||!printable(t.description,1024)) throw std::runtime_error("invalid tool display text");
        t.weaponSource=get(fields,"weaponsource");
        const auto number=[&](const char* key,double low,double high)->std::optional<double> {
            const auto value=get(fields,key);if(value.empty()) return {};
            char* end{};const auto v=std::strtod(value.c_str(),&end);
            if(end!=value.c_str()+value.size()||!std::isfinite(v)||v<low||v>high) throw std::runtime_error(std::string("invalid Tool number: ")+key);
            return v;
        };
        t.radius=number("projectileradius",.001,10);t.scale=number("projectilescale",.001,10);t.speed=number("projectilespeed",.1,1000);
        t.lifetime=number("projectilelifetime",.01,120);t.damage=number("projectiledamage",0,10000);
        // WeaponSource/SourceScript are inferred from the tool's script when omitted.
        if(!t.weaponSource.empty()&&!identifier(t.weaponSource)) throw std::runtime_error("WeaponSource must name a native gun");
        t.preloadActors=list(get(fields,"preloadactors"));t.preloadIncludes=list(get(fields,"preloadincludes"));t.preloadFiles=list(get(fields,"preloadfiles"));
        for(const auto& v:t.preloadActors) if(!identifier(v)) throw std::runtime_error("invalid PreloadActors ID");
        for(const auto& v:t.preloadIncludes) if(!path(v)) throw std::runtime_error("invalid PreloadIncludes name");
        for(const auto& v:t.preloadFiles) {
            const auto split=v.find('|');const auto kind=v.substr(0,split);
            if(split==v.npos||!path(v.substr(split+1))||(kind!="DNA"&&kind!="Script"&&kind!="Entity"&&kind!="Cho"&&kind!="Audio"&&kind!="AnimTreeProject"&&kind!="BehaviorMap"))
                throw std::runtime_error("invalid typed PreloadFiles entry");
        }
        if(const auto overlay=get(fields,"dna");!overlay.empty()) t.overlayPatch=dnaOverlayPatch(toolFile(mod.path,overlay,1u<<20),t.table.id);
        if(const auto script=get(fields,"dnascript");!script.empty()) t.dnaScriptSource=toolFile(mod.path,script,4u<<20);
        ids.insert(lower(t.table.id));tools.push_back(std::move(t));messages.push_back("Custom tool "+mod.folder+": configured "+tools.back().table.id);
    } catch(const std::exception& error) {messages.push_back("Custom tool "+mod.folder+": REJECTED: "+error.what());}
    return tools;
}

void prepareCustomTools(mods::AssetIndex& assets,const std::wstring& root,std::vector<CustomTool>& tools,
    std::vector<CustomFigure>& figures,std::vector<std::string>& messages) {
    std::string table;bool changed{};
    std::set<std::pair<std::uint32_t,std::string>> equipped;
    std::erase_if(tools,[&](CustomTool& t) {
        try {
            t.targetSkus.clear();
            for(const auto& f:figures) if(f.gameplay.enabled()&&std::any_of(t.characters.begin(),t.characters.end(),[&](const auto& c){return c=="*"||lower(c)==lower(f.name);})) t.targetSkus.push_back(f.sku);
            if(t.targetSkus.empty()) throw std::runtime_error("no enabled private gameplay character matches Characters");
            if(table.empty()) table=readAsset(assets,root,"var/toolset/toolset.bin");
            auto pendingAssets=assets;
            if(!t.assetDirectory.empty()) preparePrivateAssetDirectory(pendingAssets,t.modPath,t.assetDirectory,"tools/"+lower(t.table.id),t.assetAliases);
            NativeCatalog catalog(pendingAssets,root);
            resolveCustomTool(catalog,readToolTable(table),t,messages);
            // Best effort: without the weapons table the tool's Fire stays an extra sound.
            if(!t.weaponSource.empty()) try {
                if(const auto* rows=catalog.weapons().table("weapon")) for(const auto& [_,row]:rows->indices)
                    if(row.isTable()&&lower(row.table->text("name"))==lower(t.weaponSource)) t.fireSound=row.table->text("fireSound");
            } catch(const std::exception&) {}
            const auto candidate=extendToolTable(table,{t.table});const auto entry=info(candidate,t.table.id,t.sourceActor);
            if(!t.weaponSource.empty()&&entry.group!="Items") throw std::runtime_error("WeaponSource is supported only for Items");
            if(t.equip) for(auto sku:t.targetSkus) if(equipped.contains({sku,entry.group}))
                throw std::runtime_error("another custom tool already equips this character/group");
            const auto prefix="characters/literalcrab/tools/"+lower(t.table.id);
            t.privateDna=prefix+".dnax";t.privateScript=t.sourceScript.empty()?std::string{}:prefix+".sx";
            t.privateWeapon=t.weaponSource.empty()?std::string{}:lower(t.table.id)+"_weapon";
            if(assets.member(t.privateDna)||(!t.privateScript.empty()&&assets.member(t.privateScript))) throw std::runtime_error("custom tool resource name already claimed");
            auto dna=identifyCustomDna(readAsset(pendingAssets,root,privateAssetPath(t.assetAliases,t.sourceDna)),marker(t));
            std::string script;
            if(!t.sourceScript.empty()) {
                script=readAsset(pendingAssets,root,privateAssetPath(t.assetAliases,t.sourceScript));
                if(script.size()<0x68||script.substr(0,12)!=std::string_view("SLNG\x0c\x10\0\0\x78\x56\x34\x12",12)||word(script,12)!=script.size())
                    throw std::runtime_error("SourceScript must be native PC SLNG");
                if(!t.weaponSource.empty()) script=remapSlngLiteral(script,t.weaponSource,t.privateWeapon);
            }
            if(!t.model.empty()) (void)readAsset(pendingAssets,root,privateAssetPath(t.assetAliases,t.model+".bent"));
            std::string icon;
            const auto iconPath="ui/icons/"+lower(t.table.id)+".mtb";
            if(!t.icon.empty()) {
                auto path=t.icon;if(!lower(path).ends_with(".mtb")) path+=".mtb";
                if(pendingAssets.member(iconPath)) throw std::runtime_error("custom tool icon already claimed");
                icon=readAsset(pendingAssets,root,privateAssetPath(t.assetAliases,path));
            }
            generated(pendingAssets,t.privateDna,std::move(dna));if(!t.privateScript.empty()) generated(pendingAssets,t.privateScript,std::move(script));
            if(!icon.empty()) generated(pendingAssets,iconPath,std::move(icon));
            auto pendingFigures=figures;
            for(auto& f:pendingFigures) if(std::find(t.targetSkus.begin(),t.targetSkus.end(),f.sku)!=t.targetSkus.end()) {
                f.gameplay.extraTools.push_back(t.table.id);
                if(std::find(f.gameplay.preloadActors.begin(),f.gameplay.preloadActors.end(),t.table.actor)==f.gameplay.preloadActors.end()) f.gameplay.preloadActors.push_back(t.table.actor);
                if(t.equip) {
                    if(entry.group=="Items") {
                        f.gameplay.item=t.table.id;f.gameplay.itemWeapon={};f.gameplay.controlSources[0].clear();f.gameplay.toolDisplayNames[0].clear();
                    } else if(entry.group=="Props") {
                        f.gameplay.prop=t.table.id;f.gameplay.controlSources[1].clear();f.gameplay.toolDisplayNames[1].clear();
                    } else {
                        f.gameplay.ability=t.table.id;f.gameplay.toolDisplayNames[2].clear();
                    }
                }
            }
            assets=std::move(pendingAssets);figures=std::move(pendingFigures);
            table=candidate;changed=true;t.row=entry.row;t.hash=entry.hash;t.group=entry.group;
            if(t.equip) for(auto sku:t.targetSkus) equipped.insert({sku,entry.group});
            messages.push_back("Custom tool "+t.table.id+": private actor/resources and new native row prepared for "+std::to_string(t.targetSkus.size())+" custom character(s)");
            return false;
        } catch(const std::exception& error) {messages.push_back("Custom tool "+t.table.id+": REJECTED: "+error.what());return true;}
    });
    if(changed) generated(assets,"var/toolset/toolset.bin",std::move(table),true);
}

void setCustomToolLabels(const std::vector<CustomTool>& tools) {
    labels.clear();for(const auto& t:tools) {labels.push_back({t.table.id+"_name",t.displayName});if(!t.description.empty()) labels.push_back({t.table.id+"_description",t.description});}
}
const char* customToolText(const char* key) noexcept {
    if(!key) return nullptr;if(*key=='@') ++key;
    for(const auto& label:labels) if(label.key==key) return label.text.c_str();return nullptr;
}

std::string customToolDnaPatch(const CustomTool& t) {
    auto code=declarations({t})+helpers+R"lua(
local t=tools[1]
if type(DNA)~='table' or type(DNA.AgentGadgets)~='table' then blocked(t,'DNA shape differs');return end
local gadgets=DNA.AgentGadgets
if t.script~='' then
    if type(gadgets.Script)~='table' or (string.lower(gadgets.Script.ScriptName or '')~=t.sourceScript and
        string.lower(gadgets.Script.ScriptName or '')~=t.script and
        string.lower(gadgets.Script.ScriptName or '')~=string.lower(remap(t,t.sourceScript))) then
        blocked(t,'source script differs');return
    end
    gadgets.Script.ScriptName=t.script
end
if t.model~='' then
    if type(gadgets.EntityInstance)~='table' then blocked(t,'model gadget missing');return end
    gadgets.EntityInstance.ModelName=t.model
end
remapTree(t,DNA)
log('APPLIED: standalone custom tool DNA '..t.id)
)lua";
    return code+t.overlayPatch;
}

std::string customToolActorPatch(const std::vector<CustomTool>& tools) {
    return declarations(tools)+helpers+R"lua(
if type(Data)~='table' or type(Data.Records)~='table' then return end
for _,t in ipairs(tools) do
    local source,existing,conflict
    for _,row in ipairs(Data.Records) do
        if type(row)=='table' and type(row.Name)=='string' then
            local name=string.lower(row.Name)
            if name==t.source then if source then conflict=true end;source=row end
            if name==t.actor then if existing then conflict=true end;existing=row end
        end
    end
    if existing and (owners[existing]~=t.id or existing.Type~='Other') then conflict=true end
    if conflict then blocked(t,'actor name conflict')
    elseif source then
        if source.Type~='Other' or type(source.Parms)~='string' then blocked(t,'source actor shape differs')
        else
            local count=0
            local parms=string.gsub(source.Parms,'DNAFile=([^;]+)',function(value)
                if string.lower(value)==t.sourceDna then count=count+1;return 'DNAFile='..t.dna end
                return 'DNAFile='..value
            end)
            if count~=1 then blocked(t,'source DNA does not match actor')
            elseif existing then
                if existing.Parms~=parms then blocked(t,'owned actor definition changed') end
            else
                local row=copy(source);row.Name,row.Parms=t.actor,parms;owners[row]=t.id
                Data.Records[#Data.Records+1]=row;log('APPLIED: standalone custom tool actor '..t.actor)
            end
        end
    end
end
)lua";
}

std::string customToolDependenciesPatch(const std::vector<CustomTool>& tools) {
    return declarations(tools)+helpers+R"lua(
if type(DataMap)~='table' or type(DataMap.Actor)~='table' then return end
for _,t in ipairs(tools) do
    local source,existing,ambiguous
    for name,row in pairs(DataMap.Actor) do
        if type(name)=='string' then
            name=string.lower(name)
            if name==t.source then if source then ambiguous=true end;source=row end
            if name==t.actor then if existing then ambiguous=true end;existing=row end
        end
    end
    if ambiguous or (existing and (type(existing)~='table' or owners[existing]~=t.id)) then blocked(t,'dependency name conflict')
    elseif source and not existing then
        if type(source)~='table' or type(source.Files)~='table' then blocked(t,'source dependencies differ')
        else
            local row=copy(source);local files,includes={},{};local dna=false
            for _,file in ipairs(source.Files) do
                if type(file)~='string' then blocked(t,'invalid dependency file');return end
                local key=string.lower(file)
                if key=='dna|'..t.sourceDna then file='DNA|'..t.dna;dna=true
                elseif t.script~='' and key=='script|'..t.sourceScript then file='Script|'..t.script end
                append(files,remap(t,file))
            end
            if not dna then blocked(t,'source DNA dependency missing')
            else
                if type(source.Includes)=='table' then for _,value in ipairs(source.Includes) do append(includes,value) end end
                if t.script~='' then append(files,'Script|'..t.script) end
                if t.model~='' then append(files,'Entity|'..t.model) end
                for _,file in ipairs(t.files) do append(files,file) end
                for _,actor in ipairs(t.actors) do append(includes,actor..'|Actor') end
                for _,include in ipairs(t.includes) do append(includes,include..'|Include') end
                row.Files,row.Includes=files,includes;owners[row]=t.id;DataMap.Actor[t.actor]=row
                log('APPLIED: standalone custom tool dependencies '..t.actor)
            end
        end
    end
end
)lua";
}

std::string customToolWeaponPatch(const std::vector<CustomTool>& tools) {
    return declarations(tools)+helpers+R"lua(
if type(weapon)~='table' or type(proj)~='table' then return end
local function find(rows,name)
    local result
    for _,row in ipairs(rows) do
        if type(row)=='table' and type(row.name)=='string' and string.lower(row.name)==string.lower(name or '') then
            if result then return nil,true end;result=row
        end
    end
    return result
end
for _,t in ipairs(tools) do if t.weaponSource~='' then
    local source,a=find(weapon,t.weaponSource)
    local shot,b;if source then shot,b=find(proj,source.projectile) end
    local gun,c=find(weapon,t.weapon);local projectile,d=find(proj,t.weapon)
    if a or b or c or d or not source or source.class~='gun' or not shot or shot.class~='widget' or
        ((gun or projectile) and not (gun and projectile and owners[gun]==t.id and owners[projectile]==t.id)) then
        blocked(t,'weapon/projectile name or source conflict')
    elseif not gun then
        gun,projectile=copy(source),copy(shot);gun.name,gun.projectile=t.weapon,t.weapon;projectile.name=t.weapon
        for key,value in pairs(t.values) do
            if key=='speed' then projectile.launchSpeed={0,0,value} else projectile[key]=value end
        end
        remapTree(t,gun);remapTree(t,projectile);owners[gun],owners[projectile]=t.id,t.id
        weapon[#weapon+1],proj[#proj+1]=gun,projectile
        log('APPLIED: standalone custom tool weapon '..t.weapon)
    end
end end
)lua";
}

std::string customToolIconPatch(const std::vector<CustomTool>& tools) {
    std::string code="local fallbacks={\n";
    for(const auto& t:tools) if(t.icon.empty()) code+="["+mods::luaQuote(t.table.id)+"]="+mods::luaQuote(t.table.baseTool)+",\n";
    return code+R"lua(}
local _,_,log=...
if type(ToolSelect)~='table' or type(ToolSelect.GetIconPath)~='function' then return end
if ToolSelect.LiteralCrabCustomTools then return end
local original=ToolSelect.GetIconPath
function ToolSelect:GetIconPath(name) return original(self,fallbacks[name] or name) end
ToolSelect.LiteralCrabCustomTools=true
)lua";
}

void addCustomToolPatches(mods::Index& index,const std::vector<CustomTool>& tools) {
    if(tools.empty()) return;
    for(const auto& t:tools) {
        const auto id=marker(t),content="contains:"+id;
        index.addBuiltinPatch({t.privateDna,id,content},"custom_tool_dna_"+t.table.id,customToolDnaPatch(t),"{}");
        if(!t.dnaScriptSource.empty()) index.addBuiltinPatch({t.privateDna,id,content},"custom_tool_dna_script_"+t.table.id,t.dnaScriptSource,"{}");
        for(const auto& [from,to]:t.assetAliases) if(from.ends_with(".dnax")) {
            const auto assetId="LiteralCrabAsset:tools/"+lower(t.table.id)+":"+from;
            index.addBuiltinPatch({to,assetId,"contains:"+assetId},"custom_tool_asset_"+t.table.id+"_"+from,privateAssetDnaPatch(t.assetAliases),"{}");
        }
    }
    // Every play set's named list, like the source tool rows themselves.
    index.addBuiltinPatch({"common_actors.lua","cars_actors.lua","incredibles_actors.lua","monstersu_actors.lua","pirates_actors.lua",
        "loneranger_actors.lua","toystoryinspace_actors.lua","rumpusroom_actors.lua","contains:ActorList"},"custom_tool_actors",customToolActorPatch(tools),"{}");
    index.addBuiltinPatch({"coredatamap.lua","carsdatamap.lua","incrediblesdatamap.lua","monstersudatamap.lua","piratesdatamap.lua",
        "lonerangerdatamap.lua","toystoryinspacedatamap.lua","rumpusroomdatamap.lua","contains:DataMap =","contains:AnimTreeProject"},
        "custom_tool_dependencies",customToolDependenciesPatch(tools),"{}");
    index.addBuiltinPatch({"weapons.lua","var/weapons/weapons.lua","contains:projectileChoName"},"custom_tool_weapons",customToolWeaponPatch(tools),"{}");
    index.addBuiltinPatch({"presentation/toolselect.lua"},"custom_tool_icons",customToolIconPatch(tools),"{}");
}
}
