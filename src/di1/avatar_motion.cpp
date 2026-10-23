// SPDX-License-Identifier: GPL-3.0-only
#include "di1/avatar_motion.hpp"
#include "di1/common.hpp"
#include <cmath>
#include <cstring>

namespace crabe::di1 {
namespace {
mods::LuaApi lua;
using Count = int(__thiscall*)(void*);
using Actor = void*(__cdecl*)(std::uint32_t);
using Play = bool(__thiscall*)(void*,const char*,int,unsigned,unsigned,unsigned);
using Stop = void(__thiscall*)(void*,const char*,int,unsigned);
using Ended = bool(__thiscall*)(void*,unsigned);
using FindNode = void*(__thiscall*)(void*,void**,const char*);
using NodeOrientation = const float*(__thiscall*)(void*);
using ReleaseNode = void(__thiscall*)(void*);
void** manager{};
Count count{}; Actor actor{}; Play play{}; Stop stop{}; Ended ended{};
FindNode findNode{};NodeOrientation nodeOrientation{};ReleaseNode releaseNode{};
void* avatar(lua_State* state) noexcept {
    if(lua.type(state,1)!=3) return nullptr;
    const auto player=lua.toNumber(state,1);
    if(!std::isfinite(player) || player<0 || player>7 || player!=static_cast<int>(player)) return nullptr;
    __try {
        void* owner=*manager;
        if(!owner || player>=count(owner)) return nullptr;
        const auto* players=*reinterpret_cast<const unsigned char* const*>(static_cast<unsigned char*>(owner)+0xE8);
        if(!players) return nullptr;
        const auto handle=*reinterpret_cast<const std::uint32_t*>(players+static_cast<int>(player)*64+0x38);
        return handle?actor(handle):nullptr;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}
bool name(lua_State* state,char (&output)[128]) {
    if(lua.type(state,2)!=4) return false;
    std::size_t length{};const auto* value=lua.toLString(state,2,&length);
    if(!value || !length || length>=sizeof(output)) return false;
    for(std::size_t i=0;i<length;++i) {
        const unsigned char c=value[i];
        if(!((c>='a'&&c<='z') || (c>='A'&&c<='Z') || (c>='0'&&c<='9') || c=='_')) return false;
    }
    std::memcpy(output,value,length);output[length]='\0';return true;
}
int answer(lua_State* state,bool value) { lua.pushBoolean(state,value);return 1; }
bool start(void* target,const char* motion) noexcept {
    __try { return play(target,motion,0,0,3,14); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool finish(void* target,const char* motion) noexcept {
    __try { stop(target,motion,3,14);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool complete(void* target) noexcept {
    __try { return ended(target,14); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int __cdecl playMotion(lua_State* state) { char motion[128];auto* target=avatar(state);return answer(state,target&&name(state,motion)&&start(target,motion)); }
int __cdecl stopMotion(lua_State* state) { char motion[128];auto* target=avatar(state);return answer(state,target&&name(state,motion)&&finish(target,motion)); }
int __cdecl motionEnded(lua_State* state) { auto* target=avatar(state);return answer(state,target&&complete(target)); }
bool orientation(void* target,const char* control,float (&value)[4]) noexcept {
    void* reference{};bool valid{};
    __try {
        findNode(target,&reference,control);
        if(reference) {
            const auto* source=nodeOrientation(reference);
            if(source) {
                float norm{};valid=true;
                for(unsigned i=0;i<4;++i) {value[i]=source[i];valid=valid&&std::isfinite(value[i]);norm+=value[i]*value[i];}
                valid=valid&&norm>0.25f&&norm<4.0f;
            }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) { valid=false; }
    if(reference) {
        __try { releaseNode(&reference); }
        __except(EXCEPTION_EXECUTE_HANDLER) { valid=false; }
    }
    return valid;
}
int __cdecl socketOrientation(lua_State* state) {
    char control[128];float value[4];auto* target=avatar(state);
    if(!target || !name(state,control) || !orientation(target,control,value)) return 0;
    for(float component:value) lua.pushNumber(state,component);
    return 4;
}
}
bool addAvatarMotionAdapters(HMODULE host,mods::LuaApi& api) {
    if(!api.type || !api.toNumber || !api.toLString || !api.pushBoolean) return false;
    const auto base=reinterpret_cast<std::uintptr_t>(host);
    if(!codeMatches(base+0xADD360,"53 8B 5C 24 08 56 57 8B CB E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B F0 8B F9 85 F6 78 22 E8") ||
       !codeMatches(base+0xADD385,"8B 87 E8 00 00 00 C1 E6 06 8B 74 30 38") ||
       !callsTo(base,0xADD37C,0xAD85E0) || !codeMatches(base+0xAD85E0,"E9") ||
       !codeMatches(base+0x326030,"83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D") || !callsTo(base,0x30D89C,0x326030) ||
       !codeMatches(base+0x31F140,"51 0F B7 05") || !callsTo(base,0x31F14D,0x283E50) ||
       !callsTo(base,0x31F17C,0xD6EC00) || !callsTo(base,0x2CC04B,0xD6EC00) || !codeMatches(base+0x31F182,"C2 14 00") ||
       !codeMatches(base+0x31F190,"51 0F B7 05") || !callsTo(base,0x31F1C2,0xD6D880) || !codeMatches(base+0x31F1C8,"C2 0C 00") ||
       !codeMatches(base+0x31F1D0,"51 0F B7 05") || !callsTo(base,0x2C6746,0x31F1D0) ||
       !callsTo(base,0x31F1F8,0xD6D8B0) || !codeMatches(base+0x31F1FE,"C2 04 00")) return false;
    manager=*reinterpret_cast<void***>(base+0xADD370);
    if(!readable(manager,sizeof(void*))) return false;
    lua=api;
    count=reinterpret_cast<Count>(base+0xAD85E0);actor=reinterpret_cast<Actor>(base+0x326030);
    play=reinterpret_cast<Play>(base+0x31F140);stop=reinterpret_cast<Stop>(base+0x31F190);ended=reinterpret_cast<Ended>(base+0x31F1D0);
    api.adapters.push_back({"PlayAvatarMotion",&playMotion});api.adapters.push_back({"StopAvatarMotion",&stopMotion});
    api.adapters.push_back({"AvatarMotionEnded",&motionEnded});
    if(api.pushNumber &&
       codeMatches(base+0x333EF0,"51 8B 44 24 0C 50 C7 44 24 04 00 00 00 00") &&
       callsTo(base,0x333EFE,0x333ED0) && codeMatches(base+0x333F65,"5E 59 C2 08 00") &&
       codeMatches(base+0xD3B50,"56 8B F1 8B 46 20 66 8B 4E 24") &&
       codeMatches(base+0xD3B6E,"8D 46 38 5E C3") &&
       callsTo(base,0xB07D13,0x333EF0) && callsTo(base,0xB07D3D,0xD3B50) &&
       callsTo(base,0x333F5E,0xD3940)) {
        findNode=reinterpret_cast<FindNode>(base+0x333EF0);
        nodeOrientation=reinterpret_cast<NodeOrientation>(base+0xD3B50);
        releaseNode=reinterpret_cast<ReleaseNode>(base+0xD3940);
        api.adapters.push_back({"GetAvatarSocketOrientation",&socketOrientation});
        log("Mods: read-only avatar socket orientation query available.");
    }
    log("Mods: native avatar motion adapters available (named override channel 14).");return true;
}
}
