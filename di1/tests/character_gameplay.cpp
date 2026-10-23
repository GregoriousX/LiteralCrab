// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_gameplay.hpp"
#include "di1/character_assets.hpp"
#include "mods/lua_mods.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <stdexcept>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace crabe::di1;
namespace fs = std::filesystem;
unsigned failures{};
#define CHECK(x) do { if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); ++failures; } } while (0)
static CustomFigure figure() {
    CustomFigure f{1000900,"LiteralCrab_Zurg","LCR_Zurg","lcr_zurg","AV_Buzz","Zurg","","ui/icons/npc_mg_zurg","RumpusRoom"};
    f.gameplay={"av_zurg","characters/TS_Zurg.dnax","ITEM_ZURG_BOSS_BLASTER","","",{"tool_zurgbossblaster"},
        {"Cho|choreographies/ts_zurgball/ts_zurgball"}};
    return f;
}
static void put(std::string& bytes, std::size_t at, unsigned value) { std::memcpy(bytes.data()+at,&value,4); }
static void append(std::string& bytes, unsigned value) { const auto at=bytes.size(); bytes.resize(at+4); put(bytes,at,value); }
static std::string table() {
    std::string pool;
    const auto text=[&](const char* s) { const auto at=static_cast<unsigned>(pool.size()); pool+=s;pool+='\0';return at; };
    const auto actor=text("AV_Zurg"), other=text("OTHER"), tool=text("ITEM_ZURG_BOSS_BLASTER"), group=text("Items");
    std::string blocks[13];
    for(auto& block:blocks)block.resize(4,'\0');
    blocks[0]+=pool;
    append(blocks[1],0); append(blocks[1],0xffffffff);
    blocks[11].resize(8+76,'\0');put(blocks[11],4,1);put(blocks[11],8,tool);put(blocks[11],16,group);
    blocks[12].resize(8+16,'\0');put(blocks[12],4,2);put(blocks[12],8,actor);put(blocks[12],12,0);
    put(blocks[12],16,other);put(blocks[12],20,1);
    std::string result;
    for(auto& block:blocks){put(block,0,static_cast<unsigned>(block.size()));result+=block;}
    return result;
}
static bool run(lua_State* state,const std::string& code) {
    if(luaL_loadbuffer(state,code.data(),code.size(),"=gameplay-test") || lua_pcall(state,0,0,0)){
        std::printf("LUA: %s\n",lua_tostring(state,-1));lua_pop(state,1);return false;
    }
    return true;
}
static void inventory() {
    // Separate source/donor streams cover group ordering, duplicate tools and
    // deliberate exclusion of donor-specific innate equipment.
    std::string pool, blocks[13]; for(auto& b:blocks)b.resize(4,'\0');
    const auto text=[&](const char* s){const auto at=static_cast<unsigned>(pool.size());pool+=s;pool+='\0';return at;};
    const auto source=text("AV_Zurg"),donor=text("AV_Buzz");
    const char* names[]={"ITEM_NONE","ITEM_ZURG_BOSS_BLASTER","ITEM_BLASTER","PROP_NONE","PROP_BUZZ","ABILITY_NONE","ABILITY_BUZZ","ITEM_BUZZ"};
    blocks[11].resize(8+8*76,'\0');put(blocks[11],4,8);
    for(unsigned i=0;i<8;++i){put(blocks[11],8+i*76,text(names[i]));put(blocks[11],12+i*76,100+i);
        put(blocks[11],16+i*76,text(i==3||i==4?"Props":i==5||i==6?"Abilities":"Items"));}
    blocks[0]+=pool;
    for(const unsigned row:{0u,1u,~0u,0u,2u,7u,3u,4u,5u,6u,~0u})append(blocks[1],row);
    blocks[12].resize(24,'\0');put(blocks[12],4,2);put(blocks[12],8,source);put(blocks[12],12,0);
    put(blocks[12],16,donor);put(blocks[12],20,3);
    std::string raw;for(auto& b:blocks){put(b,0,static_cast<unsigned>(b.size()));raw+=b;}
    const auto original=raw;auto f=figure();f.gameplay.prop="PROP_BUZZ";
    bool rejected{};try{validateCustomLoadout(raw,f);}catch(const std::exception&){rejected=true;}CHECK(rejected);
    f.gameplay.inventorySource="av_buzz";f.gameplay.inventoryExclude={"ABILITY_BUZZ","ITEM_BUZZ"};
    resolveCustomInventory(raw,f);
    CHECK(f.gameplay.inventoryRows==std::vector<unsigned>({0,1,2,3,4,5,~0u}));
    CHECK(f.gameplay.inventoryHashes==std::vector<unsigned>({100,101,102,103,104,105}));
    CHECK(raw==original);
    const auto reject=[&](CustomFigure bad){bool threw{};try{resolveCustomInventory(raw,bad);}catch(const std::exception&){threw=true;}CHECK(threw);};
    auto bad=f;bad.gameplay.inventorySource="missing";reject(bad);
    bad=f;bad.gameplay.inventoryExclude={"missing"};reject(bad);
    bad=f;bad.gameplay.inventoryExclude.push_back("PROP_BUZZ");reject(bad);
    bad=f;bad.gameplay.ability="ABILITY_BUZZ";reject(bad);
    f.gameplay.inventorySource.clear();f.gameplay.inventoryExclude.clear();f.gameplay.prop.clear();resolveCustomInventory(raw,f);
    CHECK(f.gameplay.inventoryRows.empty() && f.gameplay.inventoryHashes.empty());
}
static std::string patch(const std::string& body) { return "(function(...)\n"+body+"\nend)(nil,{},function()end,_G)"; }
static void runtime() {
    using namespace crabe::mods;
    auto* state=luaL_newstate();luaL_openlibs(state);
    LuaApi api{};
    api.loadBuffer=&luaL_loadbuffer;api.getTop=&lua_gettop;api.setTop=&lua_settop;
    api.pushValue=&lua_pushvalue;api.insert=&lua_insert;api.pushClosure=&lua_pushcclosure;
    api.pcall=&lua_pcall;api.toLString=&lua_tolstring;
    for(const char* library:{"_G","table","string","math"}) {
        lua_getglobal(state,library);
        const std::string prefix=std::strcmp(library,"_G")?std::string(library)+".":"";
        for(lua_pushnil(state);lua_next(state,-2);lua_pop(state,1))
            if(lua_type(state,-2)==LUA_TSTRING && lua_iscfunction(state,-1))
                api.library.push_back({prefix+lua_tostring(state,-2),lua_tocfunction(state,-1)});
        lua_pop(state,1);
    }
    auto f=figure();f.gameplay.respawnBehavior="characters/literalcrab/test_respawn.btx";
    Index index;addCustomGameplayPatches(index,{f});
    CHECK(configure(api,std::move(index),[](const char*,...){},false));
    const std::string source="DNA={AgentGadgets={Identification={ActorTypeMask='AV_Zurg'},AiTree={BehaviorTreeFile='characters/avatar.btx'},DynamicExploder={dynexp_inheritShaders=true}},Components={ToolSetManager={}}}; return 17";
    CHECK(luaL_loadbuffer(state,source.data(),source.size(),"=original")==0);
    std::string binary;
    CHECK(lua_dump(state,[](lua_State*,const void* data,size_t length,void* out){static_cast<std::string*>(out)->append(static_cast<const char*>(data),length);return 0;},&binary)==0);
    lua_pop(state,1);
    const auto execute=[&](const std::string& bytes,const char* name,bool patched) {
        CHECK(loadBuffer(state,bytes.data(),bytes.size(),name)==0);
        // Match the game's data-only environment: library functions come from the wrapper.
        lua_newtable(state);lua_setfenv(state,-2);
        lua_pushvalue(state,-1);
        CHECK(lua_pcall(state,0,1,0)==0);
        CHECK(lua_tonumber(state,-1)==17);lua_pop(state,1);
        lua_getfenv(state,-1);lua_getfield(state,-1,"DNA");lua_getfield(state,-1,"Components");
        lua_getfield(state,-1,"ToolSetManager");lua_getfield(state,-1,"AutoStartTool1");
        CHECK(patched ? lua_isstring(state,-1) && std::string(lua_tostring(state,-1))==f.gameplay.item : lua_isnil(state,-1));
        lua_settop(state,3);lua_getfield(state,-1,"AgentGadgets");lua_getfield(state,-1,"DynamicExploder");
        lua_getfield(state,-1,"dynexp_reassemble");
        CHECK(patched ? lua_tonumber(state,-1)==1 : lua_isnil(state,-1));lua_pop(state,1);
        lua_getfield(state,-1,"dynexp_inheritShaders");CHECK(lua_toboolean(state,-1));
        lua_settop(state,4);lua_getfield(state,-1,"AiTree");lua_getfield(state,-1,"BehaviorTreeFile");
        CHECK(std::string(lua_tostring(state,-1))==(patched?f.gameplay.respawnBehavior:"characters/avatar.btx"));
        lua_settop(state,0);
    };
    execute(identifyCustomDna(binary,"LiteralCrabCustomDNA:"+f.name),"",true);
    execute(binary,"",false); // An original NPC using the same DNA stays unchanged.
    execute(identifyCustomDna(binary,"LiteralCrabCustomDNA:AnotherFigure"),"",false);
    execute(identifyCustomDna(source,"LiteralCrabCustomDNA:"+f.name),customGameplayDnaPath(f).c_str(),true);
    const std::string tree="BehaviorTree={Name='Root',Data={Children={{Name='Death',Type='RoutingNode',Data={Router={RoutingScript='Death.BTL.sx'},Children={{Name='PrepareForDeath',Type='LeafNode',Data={BehaviorType='SLEEP',HashOverrides={Duration=0.1}}}}}}}}};return 19";
    CHECK(luaL_loadbuffer(state,tree.data(),tree.size(),"=original-tree")==0);std::string treeBinary;
    CHECK(lua_dump(state,[](lua_State*,const void* data,size_t length,void* out){static_cast<std::string*>(out)->append(static_cast<const char*>(data),length);return 0;},&treeBinary)==0);lua_pop(state,1);
    for(bool tagged:{false,true}) {
        const auto bytes=tagged?identifyCustomDna(treeBinary,"LiteralCrabRespawn:"+f.name):treeBinary;
        CHECK(loadBuffer(state,bytes.data(),bytes.size(),"")==0);
        lua_newtable(state);lua_setfenv(state,-2);lua_pushvalue(state,-1);
        CHECK(lua_pcall(state,0,1,0)==0 && lua_tonumber(state,-1)==19);lua_pop(state,1);
        lua_getfenv(state,-1);lua_getfield(state,-1,"BehaviorTree");lua_getfield(state,-1,"Data");lua_getfield(state,-1,"Children");lua_rawgeti(state,-1,1);
        lua_getfield(state,-1,"Data");lua_getfield(state,-1,"Children");lua_rawgeti(state,-1,1);lua_getfield(state,-1,"Data");lua_getfield(state,-1,"HashOverrides");lua_getfield(state,-1,"Duration");
        CHECK(lua_tonumber(state,-1)==(tagged?0.001:0.1));lua_settop(state,0);
    }
    lua_close(state);
}
static void respawnBehavior() {
    auto* state=luaL_newstate();luaL_openlibs(state);
    const std::string source="BehaviorTree={Name='Root',Data={Children={{Name='Death',Type='RoutingNode',Data={Router={RoutingScript='Death.BTL.sx'},Children={{Name='PrepareForDeath',Type='LeafNode',Data={BehaviorType='SLEEP',HashOverrides={Duration=0.1,Other=17}}},{Name='SweetDeath',Data={HashOverrides={Duration=10}}}}}}}}}";
    CHECK(run(state,source));CHECK(run(state,patch(customRespawnBehaviorPatch())));
    CHECK(run(state,"local d=BehaviorTree.Data.Children[1].Data;assert(d.Children[1].Data.HashOverrides.Duration==0.001 and d.Children[1].Data.HashOverrides.Other==17 and d.Children[2].Data.HashOverrides.Duration==10)"));
    CHECK(run(state,patch(customRespawnBehaviorPatch())));
    for(const char* change:{"d.Router.RoutingScript='custom.sx'","d.Children[1].Data.HashOverrides.Duration=0.2","d.Children[3]=d.Children[1]"}) {
        CHECK(run(state,source+";d=BehaviorTree.Data.Children[1].Data;"+change));
        CHECK(run(state,patch(customRespawnBehaviorPatch())));
        CHECK(run(state,std::string("assert(d.Children[1].Data.HashOverrides.Duration==")+(std::strstr(change,"0.2")?"0.2":"0.1")+")"));
    }
    // Only zero-piece private avatars using the recognized native Avatar tree
    // select the timing variant; existing body-piece and custom trees remain.
    auto f=figure();f.gameplay.respawnBehavior="characters/literalcrab/private_respawn.btx";
    for(const char* piece:{"nil","'characters/pieces'"}) {
        CHECK(run(state,std::string("DNA={AgentGadgets={Identification={ActorTypeMask='AV_Zurg'},DynamicExploder={dynexp_reassemble=1,dynexp_pieceFile=")+piece+"},AiTree={BehaviorTreeFile='Characters/Avatar.btx'}},Components={ToolSetManager={}}}"));
        CHECK(run(state,patch(customGameplayDnaPatch(f))));
        CHECK(run(state,std::string("assert(DNA.AgentGadgets.AiTree.BehaviorTreeFile=='")+(std::strcmp(piece,"nil")?"Characters/Avatar.btx":f.gameplay.respawnBehavior)+"')"));
    }
    lua_close(state);
}
static void pure() {
    auto f=figure(); auto raw=table();
    validateCustomLoadout(raw,f);
    f.gameplay.controlSources[0]="ITEM_ZURG_BOSS_BLASTER";
    resolveCustomControlSources(raw,f);CHECK(f.gameplay.controlRows[0]==0 && f.gameplay.controlRows[1]==~0u);
    f.gameplay.controlSources[0]="MISSING";
    bool missingControl{};try{resolveCustomControlSources(raw,f);}catch(const std::exception&){missingControl=true;}CHECK(missingControl);
    f.gameplay.controlSources={};
    const auto rejected=[&](std::string bytes,CustomFigure value){bool threw{};try{validateCustomLoadout(bytes,value);}catch(const std::exception&){threw=true;}CHECK(threw);};
    auto bad=f;bad.gameplay.sourceActor="OTHER";rejected(raw,bad);
    bad=f;bad.gameplay.item="MISSING";rejected(raw,bad);
    bad=f;bad.gameplay.prop=bad.gameplay.item;bad.gameplay.item.clear();rejected(raw,bad);
    rejected(raw.substr(0,raw.size()-1),f);rejected(raw+"extra",f);
    // Relative string pointers permit longer private names without changing
    // any native VM instruction or string index.
    std::string script(0x81,'\0');std::memcpy(script.data(),"SLNG\x0c\x10\0\0\x78\x56\x34\x12",12);
    put(script,12,static_cast<unsigned>(script.size()));put(script,0x30,1);put(script,0x64,0x70-0x65);
    put(script,0x70,0x78-0x71);std::memcpy(script.data()+0x78,"ORIGINAL",8);
    const auto clonedScript=remapSlngLiteral(script,"ORIGINAL","PRIVATE_LONGER_WEAPON");
    CHECK(clonedScript.substr(0x18,0x4C)==script.substr(0x18,0x4C));
    CHECK(clonedScript.find("PRIVATE_LONGER_WEAPON")!=std::string::npos && script.find("PRIVATE_LONGER_WEAPON")==std::string::npos);
    bool badScript{};try{remapSlngLiteral(script,"MISSING","NEW");}catch(const std::exception&){badScript=true;}CHECK(badScript);
    auto tagged=identifyCustomDna("DNA={marker=17}","LiteralCrabCustomDNA:Fixture");
    CHECK(tagged.starts_with("-- LiteralCrabCustomDNA:Fixture\n"));
    auto* state=luaL_newstate();luaL_openlibs(state);CHECK(run(state,tagged));
    const char* chunk="return 'unchanged', 17";
    CHECK(luaL_loadbuffer(state,chunk,std::strlen(chunk),"=source")==0);
    std::string binary;
    CHECK(lua_dump(state,[](lua_State*,const void* data,size_t length,void* out){static_cast<std::string*>(out)->append(static_cast<const char*>(data),length);return 0;},&binary)==0);
    lua_pop(state,1);
    const auto identified=identifyCustomDna(binary,"LiteralCrabCustomDNA:Fixture");
    CHECK(identified.find("LiteralCrabCustomDNA:Fixture")!=std::string::npos);
    CHECK(luaL_loadbuffer(state,identified.data(),identified.size(),"=clone")==0 && lua_pcall(state,0,2,0)==0);
    CHECK(std::string(lua_tostring(state,-2))=="unchanged" && lua_tonumber(state,-1)==17);lua_settop(state,0);
    CHECK(run(state,"source={AgentGadgets={Identification={ActorTypeMask='AV_Zurg'}},Components={ToolSetManager={Unrelated='preserved'}}}; DNA={AgentGadgets=source.AgentGadgets,Components={ToolSetManager={Unrelated='preserved'}}}"));
    CHECK(run(state,patch(customGameplayDnaPatch(f))));
    CHECK(run(state,"assert(DNA.Components.ToolSetManager.AutoStartTool1=='ITEM_ZURG_BOSS_BLASTER'); assert(DNA.Components.ToolSetManager.Unrelated=='preserved');assert(source.Components.ToolSetManager.AutoStartTool1==nil)"));
    CHECK(run(state,"DNA.AgentGadgets={Identification={ActorTypeMask='AV_Buzz'}};DNA.Components.ToolSetManager.AutoStartTool1=nil"));
    CHECK(run(state,patch(customGameplayDnaPatch(f))));
    CHECK(run(state,"assert(DNA.Components.ToolSetManager.AutoStartTool1==nil)"));
    // Combat respawn must complete for a private zero-piece avatar. Keep an
    // explicit author choice and every original NPC's DNA untouched.
    for(const char* value:{"nil","0","1"}) {
        CHECK(run(state,std::string("DNA={AgentGadgets={Identification={ActorTypeMask='AV_Zurg'},DynamicExploder={dynexp_reassemble=")+value+",dynexp_inheritShaders=true}},Components={ToolSetManager={}}}; originalBoss={dynexp_inheritShaders=true}"));
        CHECK(run(state,patch(customGameplayDnaPatch(f))));
        CHECK(run(state,std::string("assert(DNA.AgentGadgets.DynamicExploder.dynexp_reassemble==")+(std::strcmp(value,"nil")?value:"1")+");assert(DNA.AgentGadgets.DynamicExploder.dynexp_inheritShaders==true);assert(DNA.AgentGadgets.DynamicExploder.dynexp_pieceFile==nil);assert(originalBoss.dynexp_reassemble==nil)"));
    }
    CHECK(run(state,"Data={Records={{Name='av_zurg',Type='Avatar',SubType='original',Parms='DNAFile=characters/TS_Zurg.dnax;Other=1;'}}}; original=Data.Records[1]"));
    CHECK(run(state,patch(customGameplayActorPatch({f}))));
    CHECK(run(state,patch(customGameplayActorPatch({f}))));
    CHECK(run(state,"assert(#Data.Records==2 and Data.Records[1]==original);assert(Data.Records[2].Name=='lcr_zurg');assert(Data.Records[2].Parms=='DNAFile=characters/literalcrab/literalcrab_zurg.dnax;Other=1;');assert(original.Parms=='DNAFile=characters/TS_Zurg.dnax;Other=1;')"));
    CHECK(run(state,"Data={Records={{Name='av_zurg',Type='Avatar',Parms='DNAFile=characters/TS_Zurg.dnax'}}}"));
    CHECK(run(state,patch(customGameplayActorPatch({f}))));
    CHECK(run(state,"assert(#Data.Records==2 and Data.Records[2].Parms=='DNAFile=characters/literalcrab/literalcrab_zurg.dnax')"));
    CHECK(run(state,"DataMap={Actor={av_zurg={Files={'DNA|characters/TS_Zurg.dnax','Entity|characters/TS_Zurg/TS_Zurg'},Includes={'avatar.btx|Include'}}}}; sourceFiles=DataMap.Actor.av_zurg.Files"));
    CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));
    CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));
    CHECK(run(state,"local r=DataMap.Actor.lcr_zurg;assert(r.Files[1]=='DNA|characters/literalcrab/literalcrab_zurg.dnax' and #r.Files==3);assert(#r.Includes==2 and r.Includes[2]=='tool_zurgbossblaster|Actor');assert(DataMap.Actor.av_zurg.Files==sourceFiles and #sourceFiles==2)"));
    f.gameplay.preloadIncludes={"biped_avatar"};
    CHECK(run(state,"DataMap={Actor={av_zurg={Files={'DNA|characters/TS_Zurg.dnax'},Includes={'avatar.btx|Include'}}}}; originalIncludes=DataMap.Actor.av_zurg.Includes"));
    CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));
    CHECK(run(state,"local r=DataMap.Actor.lcr_zurg;assert(#r.Includes==3 and r.Includes[3]=='biped_avatar|Include');assert(DataMap.Actor.av_zurg.Includes==originalIncludes and #originalIncludes==1)"));
    f.gameplay.itemWeapon.source="BossShot";f.gameplay.itemWeapon.privateWeapon="PrivateShot";f.gameplay.itemWeapon.radius=0.15;f.gameplay.itemWeapon.speed=45;f.gameplay.itemWeapon.scale=0.1;
    CHECK(run(state,"weapon={{name='BossShot',class='gun',projectile='BossProjectile',fireSound='original'}};proj={{name='BossProjectile',class='widget',collisionRadius=5,launchSpeed={0,0,200},damage=69}}; originalGun=weapon[1];originalProjectile=proj[1]"));
    CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));
    CHECK(run(state,"assert(#weapon==2 and #proj==2 and weapon[1]==originalGun and proj[1]==originalProjectile);assert(proj[1].collisionRadius==5 and proj[1].launchSpeed[3]==200 and proj[1].scale==nil);assert(weapon[2].name=='PrivateShot' and weapon[2].projectile=='PrivateShot' and weapon[2].fireSound=='original');assert(proj[2].collisionRadius==0.15 and proj[2].launchSpeed[3]==45 and proj[2].damage==69 and proj[2].scale==0.1)"));
    // A conflicting private name must keep the portal figure unavailable. Never
    // adopt that row, overwrite the original boss or append duplicate names.
    CHECK(run(state,"blocked={};LiteralCrab={DisableCustomFigure=function(sku) blocked[sku]=true end};Data={Records={{Name='av_zurg',Type='Avatar',Parms='DNAFile=characters/TS_Zurg.dnax'},{Name='LCR_ZURG',Type='Avatar',Parms='DNAFile=someone_else.dnax'}}}"));
    CHECK(run(state,patch(customGameplayActorPatch({f}))));
    CHECK(run(state,"assert(blocked[1000900] and #Data.Records==2 and Data.Records[2].Parms=='DNAFile=someone_else.dnax'); blocked={}; DataMap={Actor={av_zurg={Files={'DNA|characters/TS_Zurg.dnax'}},LCR_ZURG={Files={'DNA|someone_else.dnax'}}}}"));
    CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));
    CHECK(run(state,"assert(blocked[1000900] and DataMap.Actor.lcr_zurg==nil and DataMap.Actor.LCR_ZURG.Files[1]=='DNA|someone_else.dnax');blocked={}; weapon={originalGun,{name='PrivateShot',class='gun',projectile='BossProjectile'}};proj={originalProjectile}"));
    CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));
    CHECK(run(state,"assert(blocked[1000900] and #weapon==2 and #proj==1 and proj[1].collisionRadius==5);blocked={};weapon={originalGun,{name='PrivateShot'},{name='privateshot'}}"));
    CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));
    CHECK(run(state,"assert(blocked[1000900] and #weapon==3 and #proj==1);blocked={}; weapon={originalGun};proj={originalProjectile}"));
    CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));
    CHECK(run(state,patch(customWeaponDefinitionsPatch({f}))));
    CHECK(run(state,"assert(blocked[1000900]==nil and #weapon==2 and #proj==2)"));
    f.assetAliases={{"characters/ts_zurg/ts_zurg","characters/literalcrab/assets/1000900/characters/ts_zurg/ts_zurg"}};
    CHECK(run(state,"DataMap={Actor={av_zurg={Files={'DNA|characters/TS_Zurg.dnax','Entity|characters/TS_Zurg/TS_Zurg'}}}}"));
    CHECK(run(state,patch(customGameplayDependenciesPatch({f}))));
    CHECK(run(state,"assert(DataMap.Actor.av_zurg.Files[2]=='Entity|characters/TS_Zurg/TS_Zurg'); assert(DataMap.Actor.lcr_zurg.Files[2]=='Entity|characters/literalcrab/assets/1000900/characters/ts_zurg/ts_zurg')"));
    lua_close(state);
}
static void write(const fs::path& path,const std::string& bytes){fs::create_directories(path.parent_path());std::ofstream(path,std::ios::binary).write(bytes.data(),bytes.size());}
int main(int argc,char** argv){
    pure();runtime();inventory();respawnBehavior();
    if(argc==4){
        std::vector<std::string> messages;auto index=crabe::mods::Index::load(fs::path(argv[2]).wstring(),messages);
        auto figures=loadCustomFigures(index.mods,messages);
        auto assets=crabe::mods::AssetIndex::build(fs::path(argv[1]).wstring(),fs::path(argv[3]).wstring()+L"/cache",index.mods,messages);
        prepareCustomAssetDirectories(assets,figures,messages);
        prepareCustomCharacterGameplay(assets,fs::path(argv[1]).wstring(),figures,messages);
        CHECK(figures.size()==1 && figures[0].gameplay.enabled());
        if(!figures.empty()){
            write(fs::path(argv[3])/"dna-patch.lua",customGameplayDnaPatch(figures[0]));
            write(fs::path(argv[3])/"actors-patch.lua",customGameplayActorPatch(figures));
            write(fs::path(argv[3])/"dependencies-patch.lua",customGameplayDependenciesPatch(figures));
            write(fs::path(argv[3])/"weapons-patch.lua",customWeaponDefinitionsPatch(figures));
            write(fs::path(argv[3])/"respawn-patch.lua",customRespawnBehaviorPatch());
            crabe::mods::Index patches;addCustomGameplayPatches(patches,figures);
            for(const auto& script:patches.scripts) if(script.chunk=="@LiteralCrab/custom_weapon_"+figures[0].name)
                write(fs::path(argv[3])/"tool-dna-patch.lua",script.body);
            for(const auto& member:assets.members)if(member.generated){std::string bytes;CHECK(assets.read(member,bytes));write(fs::path(argv[3])/"assets"/member.name,bytes);}
        }
        for(const auto& message:messages)std::puts(message.c_str());
    }
    std::printf("Custom gameplay tests: %u failure(s).\n",failures);return failures?1:0;
}
