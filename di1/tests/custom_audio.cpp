// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_audio.hpp"
#include <cstdio>
#include <limits>
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
using namespace crabe::di1;
static unsigned failures{};
#define CHECK(x) do {if (!(x)) {std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}} while(0)
static AudioProfile profile(bool voice=false) {
    AudioProfile p;p.identity="TestFigure";p.actor="lcr_test";p.sku=1000900;p.voice=voice;
    p.dna="characters/literalcrab/testfigure.dnax";p.marker="LiteralCrabCustomDNA:TestFigure";
    AudioCue c;c.trigger="CollisionLanded";c.cooldown=2;
    c.variants={{"",std::make_shared<const std::string>("wave"),"sounds/a.wav"},
                {"",std::make_shared<const std::string>("wave2"),"sounds/b.wav"}};
    p.cues.push_back(c);p.banks={"Zurgbot_Melee.fev"};return p;
}
static void routing() {
    std::vector<AudioProfile> p{profile(),profile(true),profile(true)};
    CustomAudioPolicy policy(1);
    CHECK(!policy.choose(p,0,0,1,0,false));
    auto first=policy.choose(p,0,0,1,0,true);CHECK(first.has_value());
    CHECK(!policy.choose(p,0,0,1,1,true));
    auto second=policy.choose(p,0,0,1,2,true);CHECK(second && second!=first);
    CHECK(policy.choose(p,0,0,2,1,true));
    CHECK(!policy.choose(p,0,0,0,1,true));
    CHECK(!policy.choose(p,9,0,1,1,true));
    CHECK(!policy.choose(p,0,9,1,1,true));
    CHECK(!policy.choose(p,0,0,1,std::numeric_limits<double>::quiet_NaN(),true));
    CHECK(policy.choose(p,1,0,1,3,true));
    CHECK(!policy.choose(p,2,0,1,3,true)); // One voice cooldown across profiles.
    CHECK(policy.choose(p,2,0,2,3,true));
    CHECK(policy.choose(p,2,0,1,5,true));
    p[0].cues[0].chance=0;
    CHECK(!policy.choose(p,0,0,1,10,true));
    p[0].cues[0].chance=1;
    CHECK(!policy.choose(p,0,0,1,10.1,true)); // Failed roll still throttles repeats.
    CHECK(policy.choose(p,0,0,1,12,true));
    CHECK(!policy.idle(0,1,0,30));CHECK(!policy.idle(0,1,29,30));
    CHECK(policy.idle(0,1,30,30));CHECK(!policy.idle(0,1,30,30));
    CHECK(policy.spawned(0,1));CHECK(!policy.spawned(0,1));
    std::array<std::uint32_t,16> owners{};owners[0]=2;policy.retain(owners);
    CHECK(policy.spawned(0,1));
    policy.clear();CHECK(policy.choose(p,0,0,1,0,true));
    policy.clear();
    for (unsigned owner=1;owner<=64;++owner) CHECK(policy.spawned(3,owner));
    CHECK(!policy.spawned(3,65)); // Bounded state: never evict an active actor.
}
static bool run(lua_State* state,const std::string& code) {
    const int status=luaL_dostring(state,code.c_str());
    if (status) {std::printf("Lua: %s\n",lua_tostring(state,-1));lua_pop(state,1);}
    return status==0;
}
static std::string patch(const std::string& code) {
    return "(function(...)\n"+code+"\nend)(nil,{},function()end,_G)";
}
static void dna() {
    auto* state=luaL_newstate();luaL_openlibs(state);
    auto p=profile();
    for (const auto* name : {"Idle","Spawn","native_existing_sound"}) {
        auto cue=p.cues[0];cue.trigger=name;cue.replace=std::string(name)=="native_existing_sound";p.cues.push_back(cue);
    }
    const auto code=patch(customAudioDnaPatch(p,7));
    CHECK(run(state,"DNA={AgentGadgets={Effects={Kept=5,FX={{Type='SOUND',Event='ExploderExplode',Name='stock'}}}},Components={Kept=8}};stock=DNA.AgentGadgets.Effects.FX[1]"));
    CHECK(run(state,code));CHECK(run(state,code));
    CHECK(run(state,"local e=DNA.AgentGadgets.Effects;assert(e.Kept==5 and #e.FX==2 and e.FX[1]==stock);assert(e.FX[2].Name=='LiteralCrabAudio:7:0' and e.FX[2].Event=='CollisionLanded');assert(DNA.Components.Kept==8)"));
    CHECK(run(state,"DNA={AgentGadgets={}}"));CHECK(run(state,code));
    CHECK(run(state,"assert(#DNA.AgentGadgets.Effects.FX==1)"));
    auto tool=profile();tool.tool=true;tool.cues[0].trigger="Spawn";
    CHECK(run(state,"DNA={AgentGadgets={}}"));CHECK(run(state,patch(customAudioDnaPatch(tool,8))));
    CHECK(run(state,"assert(DNA.AgentGadgets.Effects.FX[1].Event=='Activate')"));
    CHECK(run(state,"DNA={AgentGadgets={Effects='opaque native value'}}"));CHECK(run(state,code));
    CHECK(run(state,"assert(DNA.AgentGadgets.Effects=='opaque native value')"));
    CHECK(run(state,"DataMap={Actor={native={Files={'Audio|stock'}},lcr_test={Files={'DNA|private.dnax'}}}};stock=DataMap.Actor.native"));
    const auto banks=patch(customAudioBankPatch({p,p}));CHECK(run(state,banks));CHECK(run(state,banks));
    CHECK(run(state,"assert(DataMap.Actor.native==stock and #stock.Files==1);assert(#DataMap.Actor.lcr_test.Files==2 and DataMap.Actor.lcr_test.Files[2]=='Audio|Zurgbot_Melee.fev')"));
    // The level's common preload keeps a reference, so releasing the actor never unloads a shared bank.
    CHECK(run(state,"DataMap.Playset={_loadforallplaysets_={Files={'Audio|zurgbot_melee.fev'}},other={Files={}}}"));
    CHECK(run(state,banks));CHECK(run(state,"assert(#DataMap.Playset._loadforallplaysets_.Files==1 and #DataMap.Playset.other.Files==0)"));
    CHECK(run(state,"DataMap.Playset={_loadforallplaysets_={}}"));CHECK(run(state,banks));CHECK(run(state,banks));
    CHECK(run(state,"assert(#DataMap.Playset._loadforallplaysets_.Files==1 and DataMap.Playset._loadforallplaysets_.Files[1]=='Audio|Zurgbot_Melee.fev')"));
    CHECK(run(state,"DataMap={Playset={_loadforallplaysets_={Files={}}}}"));CHECK(run(state,banks));
    CHECK(run(state,"assert(#DataMap.Playset._loadforallplaysets_.Files==1)"));
    crabe::mods::Index index;addCustomAudioPatches(index,{p});CHECK(!index.empty());
    crabe::mods::Index observed;addCustomAudioPatches(observed,{p},true);
    CHECK(observed.scripts.size()+1==index.scripts.size()); // Native dispatch needs no synthetic SOUND gadget.
    CHECK(customAudioMessage(p,p.cues[0])=="CollisionLanded");
    tool.cues[0].trigger="Fire";CHECK(customAudioMessage(tool,tool.cues[0])=="ShootGun");
    crabe::mods::Index none;addCustomAudioPatches(none,{});CHECK(none.empty());
    lua_close(state);
}
// A tool cloned from a native gun: its Fire replaces the gun's shot event; voice, other cues,
// tools without a gun and an explicit Replace stay as declared.
static void fireSounds() {
    CustomTool gun;gun.table.id="LCR_GUN";gun.fireSound="zurgboss_fire";
    CustomTool plain;plain.table.id="LCR_PLAIN";
    auto effects=profile();effects.identity="LCR_GUN";effects.tool=true;
    AudioCue fire;fire.trigger="Fire";fire.volume=0.5f;effects.cues.push_back(fire);
    auto voice=effects;voice.voice=true;
    auto other=effects;other.identity="LCR_PLAIN";
    auto manual=effects;AudioCue own;own.trigger="zurgboss_fire";own.replace=true;manual.cues.push_back(own);
    std::vector<AudioProfile> p{effects,voice,other};
    CHECK(bindToolFireSounds(p,{gun,plain})==1);
    CHECK(p[0].cues[1].replace && p[0].cues[1].trigger=="zurgboss_fire" && p[0].cues[1].volume==0.5f);
    CHECK(p[0].cues[0].trigger=="CollisionLanded" && !p[0].cues[0].replace);
    CHECK(!p[1].cues[1].replace && p[1].cues[1].trigger=="Fire" && !p[2].cues[1].replace);
    std::vector<AudioProfile> m{manual};
    CHECK(bindToolFireSounds(m,{gun})==0 && m[0].cues[1].trigger=="Fire");
}
int main() {routing();dna();fireSounds();std::printf("Custom audio policy/DNA checks: %u failure(s).\n",failures);return failures?1:0;}
