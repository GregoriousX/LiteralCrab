// SPDX-License-Identifier: GPL-3.0-only
#include "di1/avatar_motion.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
struct lua_State { int first=3,second=4;double player{};std::string name="ZURG_FIRE_SINGLESHOT";bool result{};unsigned pushes{};std::array<double,4> values{}; };
namespace {
unsigned failures{},plays{},stops{},queries{};bool fault{},missing{},complete=true;
unsigned nodeObject{},released{},lookups{};bool noNode{},nodeFault{};
std::array<float,4> quaternion{0,0,0,1};
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
std::array<unsigned char,256> owner{},players{};unsigned avatarObject{};
int __cdecl type(lua_State* s,int i){return i==1?s->first:i==2?s->second:0;}
double __cdecl number(lua_State* s,int){return s->player;}
const char* __cdecl text(lua_State* s,int,std::size_t* n){*n=s->name.size();return s->name.data();}
void __cdecl boolean(lua_State* s,int value){s->result=value!=0;++s->pushes;}
void __cdecl pushNumber(lua_State* s,double value){if(s->pushes<4)s->values[s->pushes]=value;++s->pushes;}
void* __fastcall findNode(void* self,void*,void** output,const char* control){++lookups;CHECK(self==&avatarObject && !std::strcmp(control,"Cntrl_FootCenter_Right"));*output=noNode?nullptr:&nodeObject;return output;}
const float* __fastcall nodeOrientation(void* self,void*){CHECK(self==&nodeObject);if(nodeFault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return quaternion.data();}
void __fastcall releaseNode(void* self,void*){CHECK(*static_cast<void**>(self)==&nodeObject);++released;*static_cast<void**>(self)=nullptr;}
int __fastcall count(void* self,void*){CHECK(self==owner.data());return 1;}
void* __cdecl actor(unsigned handle){CHECK(handle==0x44);return missing?nullptr:&avatarObject;}
bool __fastcall play(void* self,void*,const char* name,int mode,unsigned frame,unsigned source,unsigned layer){
    ++plays;CHECK(self==&avatarObject&&!std::strcmp(name,"ZURG_FIRE_SINGLESHOT")&&mode==0&&frame==0&&source==3&&layer==14);
    if(fault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return true;
}
void __fastcall stop(void* self,void*,const char*,int source,unsigned layer){++stops;CHECK(self==&avatarObject&&source==3&&layer==14);if(fault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);}
bool __fastcall ended(void* self,void*,unsigned layer){++queries;CHECK(self==&avatarObject&&layer==14);if(fault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return complete;}
void put(unsigned char* to,const char* pattern){while(*pattern){while(*pattern==' ')++pattern;if(!*pattern)break;if(*pattern=='?'){*to++=0;pattern+=2;}else{char* next{};*to++=static_cast<unsigned char>(std::strtoul(pattern,&next,16));pattern=next;}}}
void jump(unsigned char* at,unsigned char op,const void* dest){at[0]=op;const auto rel=static_cast<int>(reinterpret_cast<std::uintptr_t>(dest)-reinterpret_cast<std::uintptr_t>(at+5));std::memcpy(at+1,&rel,4);FlushInstructionCache(GetCurrentProcess(),at,5);}
crabe::mods::LuaCFunction lookup(const crabe::mods::LuaApi& api,const char* name){for(const auto& item:api.adapters)if(item.name==name)return item.function;return nullptr;}
bool invoke(crabe::mods::LuaCFunction f,lua_State& state){state.pushes=0;CHECK(f&&f(&state)==1&&state.pushes==1);return state.result;}
}
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));CHECK(image);if(!image)return 1;
    const std::pair<unsigned,const char*> patterns[]={
        {0xADD360,"53 8B 5C 24 08 56 57 8B CB E8 ?? ?? ?? ?? 8B 0D ?? ?? ?? ?? 8B F0 8B F9 85 F6 78 22 E8"},
        {0xADD385,"8B 87 E8 00 00 00 C1 E6 06 8B 74 30 38"},{0xAD85E0,"E9"},
        {0x326030,"83 EC 08 8B 44 24 0C 8D 0C 24 51 8B 0D"},
        {0x31F140,"51 0F B7 05"},{0x31F182,"C2 14 00"},{0x31F190,"51 0F B7 05"},
        {0x31F1C8,"C2 0C 00"},{0x31F1D0,"51 0F B7 05"},{0x31F1FE,"C2 04 00"}};
    const std::pair<unsigned,unsigned> calls[]={{0xADD37C,0xAD85E0},{0x30D89C,0x326030},{0x31F14D,0x283E50},
        {0x31F17C,0xD6EC00},{0x2CC04B,0xD6EC00},{0x31F1C2,0xD6D880},{0x2C6746,0x31F1D0},{0x31F1F8,0xD6D8B0}};
    for(const auto& [rva,pattern]:patterns)put(image+rva,pattern);
    for(const auto& [rva,to]:calls)jump(image+rva,0xE8,image+to);
    void* slot=image+0x1300000;std::memcpy(image+0xADD370,&slot,4);
    void* manager=owner.data();std::memcpy(slot,&manager,4);
    void* playerData=players.data();std::memcpy(owner.data()+0xE8,&playerData,4);
    unsigned handle=0x44;std::memcpy(players.data()+0x38,&handle,4);
    const auto api=[](){crabe::mods::LuaApi a;a.type=type;a.toNumber=number;a.toLString=text;a.pushBoolean=boolean;return a;};
    for(const auto& [rva,_]:patterns){image[rva]^=1;auto a=api();CHECK(!crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),a)&&a.adapters.empty());image[rva]^=1;}
    for(const auto& [rva,_]:calls){image[rva]^=1;auto a=api();CHECK(!crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),a));image[rva]^=1;}
    auto resolved=api();CHECK(crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),resolved));CHECK(resolved.adapters.size()==3);
    const std::pair<unsigned,const char*> socketPatterns[]={
        {0x333EF0,"51 8B 44 24 0C 50 C7 44 24 04 00 00 00 00"},
        {0x333F65,"5E 59 C2 08 00"},{0xD3B50,"56 8B F1 8B 46 20 66 8B 4E 24"},{0xD3B6E,"8D 46 38 5E C3"}};
    const std::pair<unsigned,unsigned> socketCalls[]={{0x333EFE,0x333ED0},{0xB07D13,0x333EF0},{0xB07D3D,0xD3B50},{0x333F5E,0xD3940}};
    for(const auto& [rva,pattern]:socketPatterns)put(image+rva,pattern);
    for(const auto& [rva,to]:socketCalls)jump(image+rva,0xE8,image+to);
    for(const auto& [rva,_]:socketPatterns){image[rva]^=1;auto a=api();a.pushNumber=pushNumber;CHECK(crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),a)&&!lookup(a,"GetAvatarSocketOrientation"));image[rva]^=1;}
    for(const auto& [rva,_]:socketCalls){image[rva]^=1;auto a=api();a.pushNumber=pushNumber;CHECK(crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),a)&&!lookup(a,"GetAvatarSocketOrientation"));image[rva]^=1;}
    auto withSockets=api();withSockets.pushNumber=pushNumber;
    CHECK(crabe::di1::addAvatarMotionAdapters(reinterpret_cast<HMODULE>(image),withSockets)&&withSockets.adapters.size()==4);
    const auto socket=lookup(withSockets,"GetAvatarSocketOrientation");CHECK(socket);
    jump(image+0x333EF0,0xE9,reinterpret_cast<void*>(&findNode));jump(image+0xD3B50,0xE9,reinterpret_cast<void*>(&nodeOrientation));jump(image+0xD3940,0xE9,reinterpret_cast<void*>(&releaseNode));
    jump(image+0xAD85E0,0xE9,reinterpret_cast<void*>(&count));jump(image+0x326030,0xE9,reinterpret_cast<void*>(&actor));
    jump(image+0x31F140,0xE9,reinterpret_cast<void*>(&play));jump(image+0x31F190,0xE9,reinterpret_cast<void*>(&stop));jump(image+0x31F1D0,0xE9,reinterpret_cast<void*>(&ended));
    const auto start=lookup(resolved,"PlayAvatarMotion"),finish=lookup(resolved,"StopAvatarMotion"),done=lookup(resolved,"AvatarMotionEnded");
    lua_State state;CHECK(invoke(start,state)&&plays==1);CHECK(invoke(finish,state)&&stops==1);CHECK(invoke(done,state)&&queries==1);
    complete=false;CHECK(!invoke(done,state));complete=true;
    for(double player:{-1.0,0.5,1.0,8.0,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()}){state.player=player;CHECK(!invoke(start,state));}
    state.player=0;state.first=4;CHECK(!invoke(start,state));state.first=3;state.second=3;CHECK(!invoke(start,state));state.second=4;
    for(const auto& value:{std::string(),std::string("../invalid"),std::string(128,'X'),std::string("BAD\0HIDDEN",10)}){state.name=value;CHECK(!invoke(start,state));}
    state.name="ZURG_FIRE_SINGLESHOT";CHECK(plays==1);
    missing=true;CHECK(!invoke(start,state));missing=false;
    manager=nullptr;std::memcpy(slot,&manager,4);CHECK(!invoke(start,state));manager=owner.data();std::memcpy(slot,&manager,4);
    fault=true;CHECK(!invoke(start,state));CHECK(!invoke(finish,state));CHECK(!invoke(done,state));fault=false;
    state.name="Cntrl_FootCenter_Right";state.pushes=0;CHECK(socket(&state)==4&&state.pushes==4&&released==1);
    CHECK((state.values==std::array<double,4>({0,0,0,1})));
    noNode=true;state.pushes=0;CHECK(socket(&state)==0 && state.pushes==0 && released==1);noNode=false;
    nodeFault=true;CHECK(socket(&state)==0 && released==2);nodeFault=false;
    quaternion[0]=std::numeric_limits<float>::infinity();CHECK(socket(&state)==0 && released==3);quaternion[0]=0;
    quaternion[3]=0;CHECK(socket(&state)==0 && released==4);quaternion[3]=1;
    const auto callsBefore=lookups;state.name="bad/path";CHECK(socket(&state)==0 && lookups==callsBefore);
    state.name="Cntrl_FootCenter_Right";state.player=-1;CHECK(socket(&state)==0 && lookups==callsBefore);
    CHECK(VirtualFree(image,0,MEM_RELEASE));
    std::printf("Avatar motion ABI/profile/argument checks: %u failure(s).\n",failures);return failures?1:0;
}
