// SPDX-License-Identifier: GPL-3.0-only
// Private actor-message sounds. Native playback and cutscene calls are always
// preserved unless a custom owner explicitly declares a gameplay replacement.
#include "di1/custom_audio.hpp"
#include "di1/custom_audio_playback.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <map>
#include <atomic>

namespace crabe::di1 {
namespace {
using ActorFromHandle=void*(__cdecl*)(std::uint32_t);
using PlayerCount=int(__thiscall*)(void*);
using Component=void*(__thiscall*)(void*,const char*);
using Data=void*(__cdecl*)(std::uint32_t,std::uint32_t);
using Text=const char*(__thiscall*)(void*,const char*,void*);
using Cutscene=int(__cdecl*)();
using EffectStart=void(__thiscall*)(void*,bool);
using NativePlay=std::uint32_t*(__cdecl*)(std::uint32_t*,const char*,const void*,float,float);
using NativeKill=void(__cdecl*)(std::uint32_t*);
using NativeActive=bool(__cdecl*)(std::uint32_t*);
using ActorData=void*(__thiscall*)(void*,std::uint32_t*,const std::uint32_t*);
ActorFromHandle actorFromHandle{};PlayerCount playerCount{};Component component{};
Data data{};Text text{};Cutscene cutscene{};EffectStart originalEffect{};NativePlay originalPlay{};NativeKill killSound{};
NativeActive soundActive{};
ActorData actorData{};void** actorManager{};const std::uint16_t* positionType{};
void** manager{};const std::uint16_t* toolType{};
void* effectHook{};void* playHook{};
std::vector<AudioProfile> profiles;
struct Scope {std::vector<std::string> characters;};
std::vector<Scope> scopes;
CustomAudioPolicy policy;
struct Player {std::uint32_t handle{};void* actor{};void* tools{};char identity[128]{};};
using Players=std::array<Player,8>;
Players previousPlayers{};
struct Request {std::size_t profile{},cue{};std::uint32_t actor{};};
std::map<std::string,Request,std::less<>> markers;
std::map<std::string,std::vector<Request>,std::less<>> messageCues;
bool messageRoutingEnabled{};
std::atomic<unsigned> messageReports{};
void prepareMessageCues() {
    messageCues.clear();
    for (std::size_t p=0;p<profiles.size();++p) for (std::size_t c=0;c<profiles[p].cues.size();++c) {
        const auto& cue=profiles[p].cues[c];
        if (!cue.replace && cue.trigger!="Idle" && (profiles[p].tool || cue.trigger!="Spawn"))
            messageCues[customAudioMessage(profiles[p],cue)].push_back({p,c,0});
    }
}
SRWLOCK queueLock=SRWLOCK_INIT;
std::array<Request,64> requests{};
std::size_t queued{};
struct NativeVoice {std::uint32_t handle{~0u},owner{};bool voice{};double checkAfter{},until{};};
std::array<NativeVoice,16> nativeVoices{};
double gameplayTime{};ULONGLONG previousWall{};
unsigned emitted{},errors{},spatialReports{};
// Last synchronized world position per player: a lazily dirty native body
// keeps its previous position instead of dropping a cue to 2D.
struct Place {std::uint32_t handle{};float position[3]{};};
std::array<Place,8> places{};

bool copyName(const char* input,char* output,std::size_t capacity) noexcept {
    if (!input || !capacity) return false;
    __try {
        for (std::size_t i=0;i<capacity;++i) {
            const auto c=input[i];output[i]=c;
            if (!c) return true;
            if (static_cast<unsigned char>(c)<32 || static_cast<unsigned char>(c)>126) return false;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    output[0]=0;return false;
}
bool equal(const char* input,const std::string& expected) noexcept {
    char name[128]{};return copyName(input,name,sizeof(name)) && expected==name;
}
bool snapshots(Players& out) noexcept {
    out={};
    __try {
        void* owner=*manager;if (!owner) return false;
        const auto count=playerCount(owner);if (count<1 || count>8) return false;
        const auto* players=*reinterpret_cast<const unsigned char* const*>(static_cast<unsigned char*>(owner)+0xE8);
        if (!players) return false;
        for (int i=0;i<count;++i) {
            auto& p=out[i];p.handle=*reinterpret_cast<const std::uint32_t*>(players+i*64+0x38);
            if (!p.handle) continue;
            p.actor=actorFromHandle(p.handle);if (!p.actor) {p.handle=0;continue;}
            p.tools=component(p.actor,"ToolSetManager");
            auto* properties=static_cast<unsigned char*>(data(p.handle,*toolType));
            if (properties) copyName(text(properties+8,"LiteralCrabFigure",nullptr),p.identity,sizeof(p.identity));
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {out={};return false;}
}
bool gameplay() noexcept {
    __try {return cutscene && cutscene()==0 && !customAudioPlaybackSuspended();}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool actorPosition(std::uint32_t handle,float (&position)[3]) noexcept {
    if (!actorData || !actorManager || !positionType) return false;
    __try {
        if (!*actorManager) return false;
        std::uint32_t scratch{};
        const auto* row=static_cast<const unsigned char*>(actorData(*actorManager,&scratch,&handle));
        const auto* state=row?*reinterpret_cast<const unsigned char* const*>(row+8):nullptr;
        if (!state) return false;
        const bool extended=(*reinterpret_cast<const std::uint16_t*>(state+6)&0x3FF)==*positionType;
        // Native ActorGetPosition synchronizes this lazy body when dirty. The
        // audio observer never changes game state; a dirty body uses 2D until
        // the engine has updated it normally.
        if (extended && (state[0x50]&0x80)) return false;
        const auto* value=reinterpret_cast<const float*>(state+(extended?0x20C:0x2C));
        for (unsigned i=0;i<3;++i) {
            if (!std::isfinite(value[i]) || std::abs(value[i])>1000000) return false;
            position[i]=value[i];
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void trackPlaces(const Players& players) noexcept {
    std::array<Place,8> next{};
    for (std::size_t i=0;i<players.size();++i) if (players[i].handle) {
        auto& place=next[i];place.handle=players[i].handle;
        const auto old=std::find_if(places.begin(),places.end(),[&](const auto& p){return p.handle==place.handle;});
        float position[3]{};
        if (actorPosition(place.handle,position)) {
            std::copy(std::begin(position),std::end(position),place.position);
            updateCustomAudioPosition(place.handle,position);
        } else if (old!=places.end()) std::copy(std::begin(old->position),std::end(old->position),place.position);
        else place.handle=0;
    }
    places=next;
}
bool playerPlace(std::uint32_t handle,float (&position)[3]) noexcept {
    if (actorPosition(handle,position)) return true;
    const auto found=std::find_if(places.begin(),places.end(),[&](const auto& p){return p.handle==handle;});
    if (found==places.end()) return false;
    std::copy(std::begin(found->position),std::end(found->position),position);return true;
}
const Player* context(std::size_t profile,std::uint32_t handle,const Players& players) noexcept {
    if (!handle || profile>=profiles.size()) return nullptr;
    const auto& p=profiles[profile];
    __try {
        for (const auto& player : players) {
            if (!player.handle || !player.identity[0]) continue;
            if (!p.tool) {
                if (handle==player.handle && equal(player.identity,p.identity)) return &player;
                continue;
            }
            if (std::none_of(scopes[profile].characters.begin(),scopes[profile].characters.end(),
                    [&](const auto& name){return equal(player.identity,name);}) || !player.tools) continue;
            const auto* memory=static_cast<const unsigned char*>(player.tools);
            const auto count=*reinterpret_cast<const unsigned*>(memory+0x2C0);
            auto* const* entries=*reinterpret_cast<unsigned char* const* const*>(memory+0x2BC);
            if (!entries || count>512) continue;
            for (unsigned i=0;i<count;++i) {
                auto* entry=entries[i];if (!entry) continue;
                const auto childrenCount=*reinterpret_cast<const unsigned*>(entry+8);
                auto* const* children=*reinterpret_cast<unsigned char* const* const*>(entry+4);
                if (!children || childrenCount>64) continue;
                for (unsigned child=0;child<childrenCount;++child) if (children[child] &&
                    *reinterpret_cast<const std::uint32_t*>(children[child]+0x5C)==handle &&
                    equal(*reinterpret_cast<const char* const*>(children[child]+0xC),p.actor)) return &player;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return nullptr;
}
void enqueue(Request request) noexcept {
    AcquireSRWLockExclusive(&queueLock);
    if (queued<requests.size()) requests[queued++]=request;
    ReleaseSRWLockExclusive(&queueLock);
}
void clearQueue() noexcept {
    AcquireSRWLockExclusive(&queueLock);queued=0;ReleaseSRWLockExclusive(&queueLock);
}
bool marker(const char* value,Request& result) noexcept {
    char name[160]{};if (!copyName(value,name,sizeof(name))) return false;
    if (std::strncmp(name,"LiteralCrabAudio:",17)) return false;
    const auto found=markers.find(std::string_view(name));
    if (found!=markers.end()) {result=found->second;return true;}
    return false;
}
bool readEffect(void* effect,const char*& name,std::uint32_t& actor) noexcept {
    __try {
        const auto* bytes=static_cast<const unsigned char*>(effect);
        name=*reinterpret_cast<const char* const*>(bytes+4);
        actor=*reinterpret_cast<const std::uint32_t*>(bytes+0xC);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void __fastcall soundEffect(void* self,void*,bool fallback) {
    const char* name{};std::uint32_t handle{};Request request;
    if (!readEffect(self,name,handle) || !marker(name,request)) {originalEffect(self,fallback);return;}
    if (messageRoutingEnabled) return; // Dispatch observer owns this cue; never double-play it.
    // Only our private marker is consumed. Every ordinary native effect takes
    // its original path, including while a cutscene owns the character.
    Players players;
    if (gameplay() && snapshots(players) && context(request.profile,handle,players)) {
        request.actor=handle;enqueue(request);
    }
}
std::uint32_t actorHandle(const void* actor) noexcept {
    __try {return actor?*reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(actor)+4):0;}
    __except(EXCEPTION_EXECUTE_HANDLER) {return 0;}
}
std::uint32_t* __cdecl soundPlay(std::uint32_t* result,const char* event,const void* actor,float volume,float pitch) {
    char name[160]{};Players players;
    if (result && gameplay() && copyName(event,name,sizeof(name)) && snapshots(players)) {
        const auto handle=actorHandle(actor);
        bool replaced{};
        for (std::size_t p=0;p<profiles.size();++p) if (context(p,handle,players))
            for (std::size_t c=0;c<profiles[p].cues.size();++c) {
                const auto& cue=profiles[p].cues[c];
                if (cue.replace && cue.trigger==name) {enqueue({p,c,handle});replaced=true;}
            }
        if (replaced) {*result=~0u;return result;}
    }
    return originalPlay(result,event,actor,volume,pitch);
}
void release(NativeVoice& voice) noexcept {
    if (voice.handle!=~0u && killSound) {
        __try {killSound(&voice.handle);}
        __except(EXCEPTION_EXECUTE_HANDLER) {}
    }
    voice={};
}
void stopNative(std::uint32_t owner=0) noexcept {
    for (auto& voice : nativeVoices) if (!owner || voice.owner==owner) release(voice);
}
bool playNative(const AudioVariant& variant,const AudioProfile& profile,const AudioCue& cue,
    std::uint32_t actor,std::uint32_t owner) noexcept {
    if (profile.voice && std::any_of(nativeVoices.begin(),nativeVoices.end(),
            [&](const auto& v){return v.owner==owner && v.voice && v.handle!=~0u;})) return false;
    auto slot=std::find_if(nativeVoices.begin(),nativeVoices.end(),[](const auto& voice){return voice.handle==~0u;});
    if (slot==nativeVoices.end()) return false;
    __try {
        const auto* target=actorFromHandle(actor);if (!target) return false;
        std::uint32_t handle=~0u;
        originalPlay(&handle,variant.event.c_str(),target,cue.volume,1.0f);
        if (handle==~0u) return false;
        *slot={handle,owner,profile.voice,gameplayTime+0.25,gameplayTime+30.0};return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
bool active(NativeVoice& voice) noexcept {
    if (gameplayTime<voice.checkAfter) return true; // Native sound commands may be pending.
    __try {return soundActive && soundActive(&voice.handle);}
    __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void emit(const Request& request,const Players& players) {
    const auto* player=context(request.profile,request.actor,players);if (!player) return;
    if (profiles[request.profile].voice && (customAudioVoiceActive(player->handle) ||
        std::any_of(nativeVoices.begin(),nativeVoices.end(),[&](const auto& v){return v.owner==player->handle && v.voice && v.handle!=~0u;}))) return;
    const auto chosen=policy.choose(profiles,request.profile,request.cue,player->handle,gameplayTime,true);
    if (!chosen) return;
    const auto& p=profiles[request.profile];const auto& cue=p.cues[request.cue];
    const auto& variant=cue.variants[*chosen];
    // The game's listener and native emitters both use raw world coordinates,
    // so the owning player's world position is the source (tools follow it).
    CustomAudioSpace space{};space.minDistance=cue.minDistance;space.maxDistance=cue.maxDistance;
    const bool spatial=variant.bytes && cue.spatial && playerPlace(player->handle,space.position);
    const bool played=variant.bytes?playCustomAudio(player->handle,variant.bytes,
        p.voice?CustomAudioBus::Voice:CustomAudioBus::Effects,cue.volume,spatial?&space:nullptr):
        playNative(variant,p,cue,request.actor,player->handle);
    if (played && emitted++<32) log("Custom audio %s: %s (%s, %s).",p.identity.c_str(),cue.trigger.c_str(),
        p.voice?"voice":"effect",spatial?"3D":"2D");
    float listener[3]{};
    if (played && spatial && spatialReports<8 && customAudioListener(listener)) {
        ++spatialReports;
        const auto dx=space.position[0]-listener[0],dy=space.position[1]-listener[1],dz=space.position[2]-listener[2];
        log("Custom audio 3D: source=(%.2f, %.2f, %.2f) listener=(%.2f, %.2f, %.2f) distance=%.2f min=%.1f max=%.0f.",
            space.position[0],space.position[1],space.position[2],listener[0],listener[1],listener[2],
            std::sqrt(dx*dx+dy*dy+dz*dz),space.minDistance,space.maxDistance);
    }
    if (!played && errors++<16) log("Custom audio %s: playback unavailable for %s.",p.identity.c_str(),variant.source.c_str());
}
void audioTick() noexcept {
    try {
        const auto now=GetTickCount64();const auto elapsed=previousWall?std::min(double(now-previousWall)/1000.0,0.25):0;
        previousWall=now;
        Players players;const bool available=snapshots(players);
        std::array<std::uint32_t,16> owners{};
        for (std::size_t i=0;i<players.size();++i) owners[i]=players[i].handle;
        for (const auto& old : previousPlayers) if (old.handle) {
            const auto same=std::find_if(players.begin(),players.end(),[&](const auto& p){return p.handle==old.handle && !std::strcmp(p.identity,old.identity);});
            if (same==players.end()) {
                stopCustomAudio(old.handle);stopNative(old.handle);
                auto retained=owners;for (auto& owner : retained) if (owner==old.handle) owner=0;
                policy.retain(retained);
            }
        }
        previousPlayers=players;
        policy.retain(owners);
        trackPlaces(players);
        for (auto& voice : nativeVoices) if (voice.handle!=~0u &&
            (gameplayTime>=voice.until || !active(voice) || std::find(owners.begin(),owners.end(),voice.owner)==owners.end())) release(voice);
        if (!available || !gameplay()) {clearQueue();stopAllCustomAudio();stopNative();return;}
        gameplayTime+=elapsed;
        std::array<Request,64> pending{};std::size_t count{};
        AcquireSRWLockExclusive(&queueLock);count=queued;
        std::copy_n(requests.begin(),count,pending.begin());queued=0;ReleaseSRWLockExclusive(&queueLock);
        for (std::size_t i=0;i<count;++i) emit(pending[i],players);
        for (std::size_t p=0;p<profiles.size();++p) if (!profiles[p].tool)
            for (const auto& player : players) if (player.handle && equal(player.identity,profiles[p].identity)) {
                const bool spawn=policy.spawned(p,player.handle);
                const bool idle=policy.idle(p,player.handle,gameplayTime,profiles[p].idleInterval);
                for (std::size_t c=0;c<profiles[p].cues.size();++c)
                    if ((spawn && profiles[p].cues[c].trigger=="Spawn") || (idle && profiles[p].cues[c].trigger=="Idle"))
                        emit({p,c,player.handle},players);
            }
    } catch (...) {clearQueue();stopAllCustomAudio();stopNative();}
}
bool valid(HMODULE host) {
    const auto base=reinterpret_cast<std::uintptr_t>(host);
    return codeMatches(base+0xB20C40,"51 56 8B F1 83 7E 10 FF") &&
        callsTo(base,0xB20C73,0x326030) && callsTo(base,0xB20C8B,0x31BDF0) &&
        codeMatches(base+0xB20CBF,"C2 04 00") &&
        codeMatches(base+0x31BD30,"6A FF 68 ?? ?? ?? ?? 64 A1 00 00 00 00 50") &&
        callsTo(base,0x31BD59,0x283E50) && codeMatches(base+0x31BDF0,"F3 0F 10 05") &&
        codeMatches(base+0x3190B0,"8B 44 24 04") && callsTo(base,0xB3667F,0x3190B0) &&
        codeMatches(base+0x316C70,"8B 44 24 04 50 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? C3") &&
        callsTo(base,0x316C7A,0x315C90) &&
        codeMatches(base+0x315C90,"8B 44 24 04 56 8D 71 58 50 8B CE E8") &&
        codeMatches(base+0x315CA4,"53 8A 58 18 C0 EB 04") &&
        codeMatches(base+0xD7E660,"33 C0 3B 05 ?? ?? ?? ?? 1B C0 F7 D8 C3") &&
        *reinterpret_cast<const std::uint32_t*>(base+0xD7E664)==base+0x135B488 &&
        codeMatches(base+0xADD360,"53 8B 5C 24 08 56 57 8B CB E8") &&
        callsTo(base,0xADD37C,0xAD85E0) && codeMatches(base+0xAD85E0,"E9") &&
        codeMatches(base+0x326030,"83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D") &&
        codeMatches(base+0x31F6B0,"8B 89 F0 00 00 00 85 C9 74 05 E9") &&
        codeMatches(base+0xC1EEE6,"0F B7 0D ?? ?? ?? ?? 8B 43 04 8B 40 04 56 57 51 50") &&
        codeMatches(base+0x283E50,"8B 54 24 08 8B 0D ?? ?? ?? ?? 8B 01 8B 40 10 6A 00 52") &&
        codeMatches(base+0x2CFA30,"8B 44 24 08 8B 54 24 04 50 52 E8");
}
}
void setCustomAudioMessageRouting(bool enabled) noexcept {
    messageRoutingEnabled=enabled;
    if (enabled) log("Custom audio: native actor-message observer enabled; loose clips follow the player in world-space 3D.");
}
void observeCustomAudioMessage(const char* message,std::uint32_t receiver) noexcept {
    if (!messageRoutingEnabled || !receiver) return;
    try {
        char name[128]{};
        if (!copyName(message,name,sizeof(name))) return;
        const auto found=messageCues.find(std::string_view(name));
        if (found==messageCues.end() || !gameplay()) return;
        Players players;if (!snapshots(players)) return;
        unsigned matched{};
        for (auto request:found->second) if (context(request.profile,receiver,players)) {
            request.actor=receiver;enqueue(request);++matched;
        }
        if (messageReports.fetch_add(1,std::memory_order_relaxed)<32)
            log("Custom audio message: %s receiver=%08X matched=%u.",name,receiver,matched);
    } catch (...) {} // An observer must never disrupt native dispatch.
}
bool installCustomAudio(HMODULE host,std::vector<AudioProfile> input,const std::vector<CustomFigure>& figures,
    const std::vector<CustomTool>& tools) {
    if (input.empty()) return true;
    if (effectHook || !valid(host)) {log("Custom audio: native actor/cutscene profile differs; audio left inactive.");return false;}
    profiles=std::move(input);scopes.resize(profiles.size());prepareMessageCues();
    markers.clear();
    for (std::size_t p=0;p<profiles.size();++p) for (std::size_t c=0;c<profiles[p].cues.size();++c)
        if (!profiles[p].cues[c].replace && profiles[p].cues[c].trigger!="Idle" &&
            (profiles[p].tool || profiles[p].cues[c].trigger!="Spawn"))
            markers.emplace(customAudioMarker(p,c),Request{p,c,0});
    for (std::size_t p=0;p<profiles.size();++p) if (profiles[p].tool)
        for (const auto& tool : tools) if (tool.table.id==profiles[p].identity)
            for (const auto& figure : figures) if (std::find(tool.targetSkus.begin(),tool.targetSkus.end(),figure.sku)!=tool.targetSkus.end())
                scopes[p].characters.push_back(figure.name);
    const auto base=reinterpret_cast<std::uintptr_t>(host);
    manager=*reinterpret_cast<void***>(base+0xADD370);toolType=*reinterpret_cast<const std::uint16_t* const*>(base+0xC1EEE9);
    if (!readable(manager,sizeof(void*)) || !readable(toolType,sizeof(*toolType))) {removeCustomAudio();return false;}
    actorFromHandle=reinterpret_cast<ActorFromHandle>(base+0x326030);playerCount=reinterpret_cast<PlayerCount>(base+0xAD85E0);
    component=reinterpret_cast<Component>(base+0x31F6B0);data=reinterpret_cast<Data>(base+0x283E50);text=reinterpret_cast<Text>(base+0x2CFA30);
    cutscene=reinterpret_cast<Cutscene>(base+0xD7E660);killSound=reinterpret_cast<NativeKill>(base+0x3190B0);
    soundActive=reinterpret_cast<NativeActive>(base+0x316C70);
    originalPlay=reinterpret_cast<NativePlay>(base+0x31BD30);
    if (callsTo(base,0x32604A,0x23EA30) && callsTo(base,0x3264CD,0x23EA30) &&
        codeMatches(base+0x23EA30,"8B 44 24 08 8B 00 85 C0 75 03 C2 08 00") &&
        codeMatches(base+0x3264DD,"66 8B 4E 06 BA FF 03 00 00 66 23 CA 66 3B 0D") &&
        codeMatches(base+0x3264FF,"8D 86 0C 02 00 00 EB 03 8D 46 2C")) {
        actorManager=*reinterpret_cast<void***>(base+0x32603D);
        positionType=*reinterpret_cast<const std::uint16_t* const*>(base+0x3264EC);
        if (readable(actorManager,sizeof(void*)) && readable(positionType,sizeof(*positionType)))
            actorData=reinterpret_cast<ActorData>(base+0x23EA30);
    }
    auto* target=reinterpret_cast<void*>(base+0xB20C40);
    if (MH_CreateHook(target,reinterpret_cast<void*>(&soundEffect),reinterpret_cast<void**>(&originalEffect))!=MH_OK) {removeCustomAudio();return false;}
    effectHook=target;
    const bool replacements=std::any_of(profiles.begin(),profiles.end(),[](const auto& p){return std::any_of(p.cues.begin(),p.cues.end(),[](const auto& c){return c.replace;});});
    if (replacements) {
        target=reinterpret_cast<void*>(base+0x31BD30);
        if (MH_CreateHook(target,reinterpret_cast<void*>(&soundPlay),reinterpret_cast<void**>(&originalPlay))!=MH_OK) {removeCustomAudio();return false;}
        playHook=target;
    }
    if (!installCustomAudioPlayback(host,&audioTick) || MH_EnableHook(effectHook)!=MH_OK ||
        (playHook && MH_EnableHook(playHook)!=MH_OK)) {removeCustomAudio();return false;}
    previousWall=0;gameplayTime=0;emitted=errors=spatialReports=0;messageReports.store(0,std::memory_order_relaxed);policy.clear();clearQueue();
    previousPlayers={};places={};
    log("Custom audio: %u private effects/voice profile(s); cutscene audio preserved.",static_cast<unsigned>(profiles.size()));
    return true;
}
void removeCustomAudio() noexcept {
    if (effectHook) {MH_DisableHook(effectHook);MH_RemoveHook(effectHook);effectHook=nullptr;}
    if (playHook) {MH_DisableHook(playHook);MH_RemoveHook(playHook);playHook=nullptr;}
    messageRoutingEnabled=false;messageCues.clear();
    clearQueue();stopNative();removeCustomAudioPlayback();profiles.clear();scopes.clear();markers.clear();policy.clear();
    originalEffect=nullptr;originalPlay=nullptr;manager=nullptr;toolType=nullptr;
    actorData=nullptr;actorManager=nullptr;positionType=nullptr;previousPlayers={};places={};
}
}
