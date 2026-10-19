// SPDX-License-Identifier: GPL-3.0-only
// Real adapter calls and MinHook interoperation on an owned x86 image. Optional
// original-EXE mapping validates the profile; never executes its entry point.
#include "di1/adapters.hpp"
#include "minhook/MinHook.h"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <string_view>
#include <vector>

struct lua_State {
    int kinds[4]{0, 3, 4, 1};
    double player{};
    std::string name = "ITEM_FLINTLOCK";
    bool force{}, answer{};
    unsigned pushes{};
};
namespace {
int failures{};
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
constexpr unsigned queryRva = 0xC1A600, slotRva = 0x1300000;
struct Pattern { unsigned rva; const char* bytes; };
const Pattern patterns[] = {
    {0xADD360,"53 8B 5C 24 08 56 57 8B CB E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B F0 8B F9 85 F6 78 22 E8"},
    {0xADD385,"8B 87 E8 00 00 00 C1 E6 06 8B 74 30 38"}, {0xAD85E0,"E9"},
    {0x326030,"83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D"},
    {0x31F6B0,"8B 89 F0 00 00 00 85 C9 74 05 E9"}, {0x5FFAA4,"68 ?? ?? ?? ?? 8B C8 E8"},
    {0xC1F130,"51 8B 44 24 08 53 57 8B D9 8B 0D ?? ?? ?? ?? 50 E8"},
    {0xC1F6F0,"8B 44 24 04 8B 49 20 6A 00 6A 00 50 E8"},
    {0xC1F6D0,"8B 44 24 04 8B 49 20 6A 00 6A 01 50 E8"},
    {0xC1F1D0,"51 8B 44 24 08 55 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8"},
    {queryRva,"8B 44 24 04 53 55 56 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8"},
    {0xC1A660,"8B 44 24 04 53 55 56 57 8B F9 8B 0D ?? ?? ?? ?? 50 E8"}
};
const std::pair<unsigned,unsigned> calls[] = {
    {0xADD37C,0xAD85E0},{0x30D89C,0x326030},{0x5FFAAB,0x31F6B0},
    {0xC1F6FC,0xC1F130},{0xC1F6DC,0xC1F130},{0xC1F718,0xC1F1D0},
    {0x5FFBEB,queryRva},{0xC1A6AC,0xC19E10}
};
void put(unsigned char* dest, const char* text) {
    while (*text) {
        while (*text==' ') ++text;
        if (!*text) break;
        if (*text=='?') { *dest++=0; text+=2; }
        else { char* next{}; *dest++=static_cast<unsigned char>(std::strtoul(text,&next,16)); text=next; }
    }
}
void relative(unsigned char* dest, unsigned char opcode, const void* target) {
    unsigned char bytes[5]{opcode};
    const auto rel = static_cast<int>(reinterpret_cast<std::uintptr_t>(target)-reinterpret_cast<std::uintptr_t>(dest+5));
    std::memcpy(bytes+1,&rel,4);
    DWORD old{}; CHECK(VirtualProtect(dest,5,PAGE_EXECUTE_READWRITE,&old));
    std::memcpy(dest,bytes,5); DWORD ignored{}; CHECK(VirtualProtect(dest,5,old,&ignored));
    FlushInstructionCache(GetCurrentProcess(),dest,5);
}
int __cdecl kind(lua_State* s,int index) { return index>=1 && index<=3 ? s->kinds[index] : 0; }
double __cdecl number(lua_State* s,int) { return s->player; }
int __cdecl boolean(lua_State* s,int) { return s->force; }
const char* __cdecl string(lua_State* s,int,std::size_t* length) { *length=s->name.size(); return s->name.data(); }
void __cdecl pushBoolean(lua_State* s,int value) { s->answer=value!=0; ++s->pushes; }
crabe::mods::LuaCFunction lookup(const std::vector<crabe::mods::LibraryFunction>& api,std::string_view name) {
    for (const auto& item:api) if(item.name==name) return item.function;
    return nullptr;
}
std::array<unsigned char,0x100> manager{}, players{};
unsigned actor[2]{0,0x12345678}, tools[2]{};
unsigned awardCalls{}, unawardCalls{}, queryCalls{}, componentCalls{};
int observedForce{}, observedSecondary{};
bool awarded{}, unlocked=true, fault{}, actorMissing{}, componentMissing{};
void* observedSelf{};
int __fastcall count(void* self,void*) { CHECK(self==manager.data()); return 2; }
void* __cdecl fromHandle(unsigned handle) { CHECK(handle==0x12345678); return actorMissing ? nullptr : actor; }
void* __fastcall component(void* self,void*,const char* name) {
    ++componentCalls; CHECK(self==actor && !std::strcmp(name,"ToolSetManager"));
    return componentMissing ? nullptr : tools;
}
bool __fastcall query(void* self,void*,const char* name) {
    observedSelf=self; ++queryCalls;
    if(fault) RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    return self==tools && !std::strcmp(name,"ITEM_FLINTLOCK") && awarded;
}
bool __fastcall queryUnlocked(void* self,void*,const char* name) {
    observedSelf=self; ++queryCalls;
    return self==tools && !std::strcmp(name,"ITEM_FLINTLOCK") && unlocked;
}
void __fastcall award(void* self,void*,const char* name,int force,int secondary) {
    ++awardCalls; observedSelf=self; observedForce=force; observedSecondary=secondary;
    if(!std::strcmp(name,"ITEM_FLINTLOCK")) awarded=true;
}
void __fastcall unaward(void* self,void*,const char* name) {
    ++unawardCalls; observedSelf=self;
    if(!std::strcmp(name,"ITEM_FLINTLOCK")) awarded=false;
}
bool __fastcall other(void*,void*,const char*) { return false; }
bool invoke(crabe::mods::LuaCFunction fn,lua_State& state) {
    state.answer=false; state.pushes=0;
    CHECK(fn && fn(&state)==1 && state.pushes==1);
    return state.answer;
}
}
int wmain(int argc,wchar_t** argv) {
    CHECK(MH_Initialize()==MH_OK);
    crabe::mods::LuaApi lua;
    lua.type=kind; lua.toNumber=number; lua.toBoolean=boolean; lua.toLString=string; lua.pushBoolean=pushBoolean;
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    if(!image) return 1;
    for(const auto& p:patterns) put(image+p.rva,p.bytes);
    for(const auto& [site,target]:calls) relative(image+site,0xE8,image+target);
    void* slot=image+slotRva; std::memcpy(image+0xADD370,&slot,4);
    const char* componentName="ToolSetManager"; std::memcpy(image+0x5FFAA5,&componentName,4);
    const auto resolve=[&](crabe::di1::ToolQueryHook hook=crabe::di1::ToolQueryHook{}) {
        return crabe::di1::resolveAdapters(reinterpret_cast<HMODULE>(image),lua,hook);
    };
    CHECK(lookup(resolve(),"AwardTool"));
    for(const auto& p:patterns) {
        image[p.rva]^=1; CHECK(!lookup(resolve(),"AwardTool")); image[p.rva]^=1;
    }
    for(const auto& [site,target]:calls) {
        (void)target; image[site]^=1; CHECK(!lookup(resolve(),"AwardTool")); image[site]^=1;
    }
    void* trampoline{};
    CHECK(MH_CreateHook(image+queryRva,reinterpret_cast<void*>(&queryUnlocked),&trampoline)==MH_OK);
    CHECK(MH_EnableHook(image+queryRva)==MH_OK);
    const crabe::di1::ToolQueryHook owned{trampoline,reinterpret_cast<void*>(&queryUnlocked)};
    CHECK(!lookup(resolve(),"AwardTool")); // Unidentified E9 is still rejected.
    CHECK(lookup(resolve(owned),"AwardTool"));
    CHECK(!lookup(resolve({trampoline,reinterpret_cast<void*>(&other)}),"AwardTool"));
    CHECK(!lookup(resolve({nullptr,owned.detour}),"AwardTool"));
    CHECK(!lookup(resolve({reinterpret_cast<void*>(1),owned.detour}),"AwardTool"));
    auto* copied=static_cast<unsigned char*>(trampoline);
    for(unsigned offset:{0u,5u,6u}) {
        copied[offset]^=1; CHECK(!lookup(resolve(owned),"AwardTool")); copied[offset]^=1;
    }
    image[queryRva+5]^=1; CHECK(!lookup(resolve(owned),"AwardTool")); image[queryRva+5]^=1;
    auto api=resolve(owned);
    CHECK(MH_DisableHook(image+queryRva)==MH_OK); CHECK(MH_RemoveHook(image+queryRva)==MH_OK);
    // Replace engine dependencies only after profile checks; execute no game code.
    for(const auto& [rva,fn]:std::initializer_list<std::pair<unsigned,const void*>>{
        {0xAD85E0,reinterpret_cast<void*>(&count)}, {0x326030,reinterpret_cast<void*>(&fromHandle)},
        {0x31F6B0,reinterpret_cast<void*>(&component)}, {0xC1F130,reinterpret_cast<void*>(&award)},
        {0xC1F1D0,reinterpret_cast<void*>(&unaward)}, {queryRva,reinterpret_cast<void*>(&queryUnlocked)},
        {0xC1A660,reinterpret_cast<void*>(&query)}}) relative(image+rva,0xE9,fn);
    *reinterpret_cast<void**>(image+slotRva)=manager.data();
    *reinterpret_cast<void**>(manager.data()+0xE8)=players.data();
    for(unsigned index=0;index!=2;++index) *reinterpret_cast<unsigned*>(players.data()+index*64+0x38)=actor[1];
    lua_State state;
    const auto awardFn=lookup(api,"AwardTool"), unawardFn=lookup(api,"UnAwardTool");
    const auto hasFn=lookup(api,"HasToolSet"), isFn=lookup(api,"IsToolAwarded"), unlockedFn=lookup(api,"IsToolUnlocked");
    CHECK(awardFn && unawardFn && hasFn && isFn && unlockedFn);
    for(int player=0;player!=2;++player) {
        state.player=player; awarded=false; const auto before=awardCalls;
        state.force=player==1;
        CHECK(invoke(hasFn,state)); CHECK(!invoke(isFn,state)); CHECK(invoke(unlockedFn,state));
        CHECK(invoke(awardFn,state) && awardCalls==before+1 && observedSelf==tools);
        CHECK(observedForce==player && observedSecondary==0);
        CHECK(invoke(awardFn,state) && awardCalls==before+1); // Idempotent.
        CHECK(invoke(isFn,state));
        const auto removed=unawardCalls;
        CHECK(invoke(unawardFn,state) && unawardCalls==removed+1 && !awarded);
        CHECK(invoke(unawardFn,state) && unawardCalls==removed+1);
    }
    const auto beforeAwards=awardCalls, beforeRemovals=unawardCalls;
    for(double player:{-1.,2.,7.,8.,0.5,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}) {
        state.player=player; CHECK(!invoke(awardFn,state)); CHECK(!invoke(unawardFn,state)); CHECK(!invoke(hasFn,state));
    }
    state.player=0;
    for(int index:{1,2}) { const auto old=state.kinds[index]; state.kinds[index]=0; CHECK(!invoke(awardFn,state)); state.kinds[index]=old; }
    for(const auto& name:{std::string(),std::string("bad name"),std::string(64,'A'),std::string("ITEM_FLINTLOCK\0tail",19)}) {
        state.name=name; CHECK(!invoke(awardFn,state));
    }
    CHECK(awardCalls==beforeAwards && unawardCalls==beforeRemovals);
    state.name="ITEM_FLINTLOCK";
    actorMissing=true; CHECK(!invoke(hasFn,state) && !invoke(awardFn,state)); actorMissing=false;
    componentMissing=true; CHECK(!invoke(hasFn,state) && !invoke(awardFn,state)); componentMissing=false;
    *reinterpret_cast<void**>(image+slotRva)=nullptr; CHECK(!invoke(hasFn,state) && !invoke(awardFn,state));
    *reinterpret_cast<void**>(image+slotRva)=manager.data();
    fault=true; CHECK(!invoke(isFn,state)); fault=false;
    unlocked=false; CHECK(!invoke(unlockedFn,state));
    VirtualFree(image,0,MEM_RELEASE);
    if(argc>1) {
        HMODULE host=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        CHECK(host!=nullptr);
        if(host) {
            auto* base=reinterpret_cast<unsigned char*>(host);
            std::array<unsigned char,64> saved{}; std::memcpy(saved.data(),base+queryRva,saved.size());
            CHECK(lookup(crabe::di1::resolveAdapters(host,lua),"AwardTool"));
            CHECK(MH_CreateHook(base+queryRva,reinterpret_cast<void*>(&queryUnlocked),&trampoline)==MH_OK);
            CHECK(MH_EnableHook(base+queryRva)==MH_OK);
            CHECK(!lookup(crabe::di1::resolveAdapters(host,lua),"AwardTool"));
            CHECK(lookup(crabe::di1::resolveAdapters(host,lua,{trampoline,reinterpret_cast<void*>(&queryUnlocked)}),"AwardTool"));
            CHECK(MH_DisableHook(base+queryRva)==MH_OK); CHECK(MH_RemoveHook(base+queryRva)==MH_OK);
            CHECK(!std::memcmp(saved.data(),base+queryRva,saved.size())); FreeLibrary(host);
        }
    }
    CHECK(MH_Uninitialize()==MH_OK);
    std::printf("Tool adapters/profile/owned hook/ABI: %s\n",failures?"FAILED":"passed");
    return failures?1:0;
}
