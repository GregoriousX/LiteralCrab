// SPDX-License-Identifier: GPL-3.0-only
// Exercise the production actor/cutscene routing against an owned native-layout
// fixture, without starting the game, changing native sound or opening a device.
#include "di1/custom_audio_runtime.cpp"
#include <cstdio>
using namespace crabe::di1;
namespace {
unsigned failures{},ordinaryEffects{},ordinarySounds{},customClips{},stopped{},killed{},moves{};
bool inCutscene{},fault{},nativePlaying=true,lastSpatial{};
CustomAudioSpace lastSpace{};
std::array<unsigned char,0x140> actorBytes{},toolBytes{},npcBytes{},managerBytes{},properties{};
std::array<unsigned char,0x300> toolsBytes{};
std::array<unsigned char,64*8> playerBytes{};
std::array<unsigned char,0x80> entry{},child{},effect{};
std::array<unsigned char,0x240> stateBytes{};
std::array<unsigned char,16> stateRow{};
unsigned char* entryPtr=entry.data();unsigned char* childPtr=child.data();
void* managerPtr=managerBytes.data();
std::uint16_t typeValue=17;
const char* identity="Figure";
template<class T> void set(unsigned char* target,std::size_t at,T value) {std::memcpy(target+at,&value,sizeof(value));}
#define CHECK(x) do {if (!(x)) {std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}} while(0)
void* __cdecl getActor(std::uint32_t handle) {
    return handle==101?actorBytes.data():handle==202?toolBytes.data():handle==303?npcBytes.data():nullptr;
}
int __fastcall countPlayers(void*,void*) {return 1;}
void* __fastcall getComponent(void* a,void*,const char*) {return a==actorBytes.data()?toolsBytes.data():nullptr;}
void* __cdecl getData(std::uint32_t handle,std::uint32_t type) {
    if (fault) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    return handle==101 && type==17?properties.data():nullptr;
}
const char* __fastcall getText(void*,void*,const char*,void*) {return identity;}
int __cdecl cutscenePlaying() {return inCutscene?1:0;}
void __fastcall nativeEffect(void*,void*,bool) {++ordinaryEffects;}
std::uint32_t* __cdecl nativeSound(std::uint32_t* result,const char*,const void*,float,float) {
    ++ordinarySounds;*result=999;return result;
}
void __cdecl kill(std::uint32_t* handle) {CHECK(*handle==77);*handle=~0u;++killed;}
bool __cdecl isActive(std::uint32_t*) {return nativePlaying;}
void* __fastcall getActorData(void*,void*,std::uint32_t*,const std::uint32_t*) {return stateRow.data();}
AudioProfile makeProfile(bool tool=false) {
    AudioProfile p;p.identity=tool?"LCR_TOOL":"Figure";p.actor=tool?"lcr_tool_test":"lcr_figure";p.tool=tool;
    AudioCue cue;cue.trigger=tool?"ShootGun":"CollisionLanded";cue.cooldown=0;
    cue.variants.push_back({"",std::make_shared<const std::string>("fixture"),"audio/test.wav"});p.cues.push_back(cue);
    return p;
}
void setup() {
    profiles={makeProfile(),makeProfile(true)};scopes.resize(2);scopes[1].characters={"Figure"};
    messageRoutingEnabled=false;prepareMessageCues();
    markers.clear();markers.emplace(customAudioMarker(0,0),Request{0,0,0});markers.emplace(customAudioMarker(1,0),Request{1,0,0});
    policy.clear();clearQueue();nativeVoices={};gameplayTime=0;previousWall=0;places={};
    actorFromHandle=&getActor;playerCount=reinterpret_cast<PlayerCount>(&countPlayers);component=reinterpret_cast<Component>(&getComponent);data=&getData;text=reinterpret_cast<Text>(&getText);
    cutscene=&cutscenePlaying;originalEffect=reinterpret_cast<EffectStart>(&nativeEffect);originalPlay=&nativeSound;killSound=&kill;soundActive=&isActive;
    manager=&managerPtr;toolType=&typeValue;
    set(managerBytes.data(),0xE8,playerBytes.data());set(playerBytes.data(),0x38,101u);
    set(actorBytes.data(),4,101u);set(toolBytes.data(),4,202u);set(npcBytes.data(),4,303u);
    set(toolsBytes.data(),0x2BC,&entryPtr);set(toolsBytes.data(),0x2C0,1u);
    set(entry.data(),4,&childPtr);set(entry.data(),8,1u);set(child.data(),0xC,"lcr_tool_test");set(child.data(),0x5C,202u);
    identity="Figure";inCutscene=fault=false;
}
void send(std::size_t profile,std::uint32_t actor) {
    const auto name=customAudioMarker(profile,0);
    set(effect.data(),4,name.c_str());set(effect.data(),0xC,actor);
    soundEffect(effect.data(),nullptr,false);
}
void routing() {
    setup();send(0,101);CHECK(queued==1);audioTick();CHECK(customClips==1 && ordinaryEffects==0);
    actorData=reinterpret_cast<ActorData>(&getActorData);actorManager=&managerPtr;positionType=&typeValue;
    set(stateRow.data(),8,stateBytes.data());set(stateBytes.data(),0x2C,1.0f);set(stateBytes.data(),0x30,2.0f);set(stateBytes.data(),0x34,3.0f);
    float position[3]{};CHECK(actorPosition(101,position) && position[2]==3);
    set(stateBytes.data(),6,typeValue);set(stateBytes.data(),0x20C,4.0f);set(stateBytes.data(),0x210,5.0f);set(stateBytes.data(),0x214,6.0f);
    CHECK(actorPosition(101,position) && position[0]==4);
    stateBytes[0x50]=0x80;const auto oldState=stateBytes;
    CHECK(!actorPosition(101,position) && stateBytes==oldState);stateBytes[0x50]=0;
    actorData=nullptr;
    send(0,303);CHECK(queued==0); // A stock/NPC actor cannot use a player's private marker.
    send(1,202);CHECK(queued==1);audioTick();CHECK(customClips==2);
    set(child.data(),0x5C,404u);send(1,202);CHECK(queued==0);set(child.data(),0x5C,202u);
    set(child.data(),0xC,"other_tool");send(1,202);CHECK(queued==0);set(child.data(),0xC,"lcr_tool_test");
    identity="OtherFigure";send(0,101);send(1,202);CHECK(queued==0);identity="Figure";
    set(effect.data(),4,"stock_sound");soundEffect(effect.data(),nullptr,false);CHECK(ordinaryEffects==1);
    // Cutscenes always retain the exact original sound call, including an
    // event whose custom owner requested gameplay replacement.
    auto replacement=profiles[0].cues[0];replacement.trigger="stock_sound";replacement.replace=true;
    profiles[0].cues.push_back(replacement);
    std::uint32_t result{};
    soundPlay(&result,"stock_sound",actorBytes.data(),0.7f,1.2f);CHECK(result==~0u && queued==1);
    clearQueue();inCutscene=true;send(0,101);send(1,202);CHECK(queued==0);
    soundPlay(&result,"stock_sound",actorBytes.data(),0.7f,1.2f);CHECK(result==999 && ordinarySounds==1);
    nativeVoices[0]={77,101,true,0,30};audioTick();CHECK(killed==1 && stopped>0 && customClips==2);
    inCutscene=false;
    soundPlay(&result,"stock_sound",npcBytes.data(),1,1);CHECK(result==999 && ordinarySounds==2);
    fault=true;send(0,101);CHECK(queued==0);fault=false;
    // Lifecycle idle/spawn cues never catch up while a cinematic blocks audio.
    auto spawn=profiles[0].cues[0];spawn.trigger="Spawn";profiles[0].cues.push_back(spawn);
    policy.clear();audioTick();CHECK(customClips==3);audioTick();CHECK(customClips==3);
    inCutscene=true;audioTick();inCutscene=false;audioTick();CHECK(customClips==3);
    set(playerBytes.data(),0x38,0u);audioTick();CHECK(queued==0);
    set(playerBytes.data(),0x38,101u);audioTick();CHECK(customClips==4);
    // Bounded corrupt native inventory metadata never widens ownership.
    set(toolsBytes.data(),0x2C0,513u);send(1,202);CHECK(queued==0);
}
void messageRouting() {
    setup();
    // The listener and native emitters share raw world space: the owning
    // player's world position is the 3D source for both character and tool.
    actorData=reinterpret_cast<ActorData>(&getActorData);actorManager=&managerPtr;positionType=&typeValue;
    stateBytes.fill(0);set(stateRow.data(),8,stateBytes.data());
    set(stateBytes.data(),6,typeValue);set(stateBytes.data(),0x20C,100000.0f);set(stateBytes.data(),0x210,50000.0f);
    profiles[0].cues[0].trigger="Land";profiles[1].cues[0].trigger="Fire";prepareMessageCues();
    profiles[1].cues[0].minDistance=4;profiles[1].cues[0].maxDistance=900;
    setCustomAudioMessageRouting(true);
    observeCustomAudioMessage("CollisionLanded",303);CHECK(queued==0);
    observeCustomAudioMessage("CollisionLanded",101);CHECK(queued==1);
    // Legacy DNA markers must not duplicate a directly observed message.
    send(0,101);CHECK(queued==1);
    const auto before=customClips;audioTick();CHECK(customClips==before+1);
    CHECK(lastSpatial && lastSpace.position[0]==100000.0f && lastSpace.position[1]==50000.0f && lastSpace.minDistance==5);
    CHECK(moves>0);
    observeCustomAudioMessage("ShootGun",101);CHECK(queued==0);
    // A lazily dirty body keeps the last synchronized position for the tool.
    stateBytes[0x50]=0x80;
    observeCustomAudioMessage("ShootGun",202);CHECK(queued==1);
    audioTick();CHECK(customClips==before+2);
    CHECK(lastSpatial && lastSpace.position[0]==100000.0f && lastSpace.minDistance==4 && lastSpace.maxDistance==900);
    stateBytes[0x50]=0;
    // Spatial=0 keeps an explicitly local cue in 2D.
    profiles[1].cues[0].spatial=false;policy.clear();
    observeCustomAudioMessage("ShootGun",202);audioTick();CHECK(customClips==before+3 && !lastSpatial);
    profiles[1].cues[0].spatial=true;
    // Without any known position the cue still plays, in 2D.
    actorData=nullptr;places={};policy.clear();
    observeCustomAudioMessage("ShootGun",202);audioTick();CHECK(customClips==before+4 && !lastSpatial);
    identity="OtherFigure";observeCustomAudioMessage("ShootGun",202);CHECK(queued==0);identity="Figure";
    inCutscene=true;observeCustomAudioMessage("ShootGun",202);CHECK(queued==0);inCutscene=false;
    observeCustomAudioMessage("unknown",202);observeCustomAudioMessage("CollisionLanded",0);CHECK(queued==0);
    setCustomAudioMessageRouting(false);observeCustomAudioMessage("ShootGun",202);CHECK(queued==0);
}
}
namespace crabe::di1 {
void log(const char*,...) {}
bool readable(const void* p,std::size_t n) noexcept {return p && n;}
bool codeMatches(std::uintptr_t,const char*) noexcept {return false;}
bool callsTo(std::uintptr_t,std::uint32_t,std::uint32_t) noexcept {return false;}
bool installCustomAudioPlayback(HMODULE,CustomAudioTick) {return true;}
bool playCustomAudio(std::uint32_t owner,std::shared_ptr<const std::string> bytes,CustomAudioBus,float,const CustomAudioSpace* space) noexcept {
    CHECK(owner==101 && bytes && *bytes=="fixture");
    lastSpatial=space!=nullptr;if (space) lastSpace=*space;
    ++customClips;return true;
}
void stopCustomAudio(std::uint32_t) noexcept {}
void requestNativeEventData(const char*) noexcept {}
void stopAllCustomAudio() noexcept {++stopped;}
void setCustomAudioPaused(bool) noexcept {}
bool customAudioPlaybackSuspended() noexcept {return false;}
bool customAudioVoiceActive(std::uint32_t) noexcept {return false;}
void updateCustomAudioPosition(std::uint32_t owner,const float* position) noexcept {
    CHECK(owner==101 && position);++moves;
}
bool customAudioListener(float (&position)[3]) noexcept {position[0]=position[1]=position[2]=0;return true;}
void removeCustomAudioPlayback() noexcept {}
}
extern "C" {
MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*) {return MH_ERROR_UNSUPPORTED_FUNCTION;}
MH_STATUS WINAPI MH_EnableHook(LPVOID) {return MH_OK;}
MH_STATUS WINAPI MH_DisableHook(LPVOID) {return MH_OK;}
MH_STATUS WINAPI MH_RemoveHook(LPVOID) {return MH_OK;}
}
int main() {routing();messageRouting();std::printf("Custom audio native routing checks: %u failure(s).\n",failures);return failures?1:0;}
