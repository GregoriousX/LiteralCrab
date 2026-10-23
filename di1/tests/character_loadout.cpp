// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_loadout.hpp"
#include "minhook/MinHook.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <cstdlib>
using namespace crabe::di1;
namespace {
unsigned failures{};
#define CHECK(x) do{if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);++failures;}}while(0)
ToolInitializationCallback callback{};
std::array<unsigned char,0x400> tools{};
std::array<unsigned char,16> actor{},properties{};
std::array<unsigned char,0x60> nativeTable{};
std::array<unsigned char,76> nativeRow{};
std::array<unsigned char,0x80> item{},prop{},ability{},unrelated{};
std::array<unsigned char,0x90> child{};
unsigned char* childPointer=child.data();
std::array<unsigned char*,4> entries{item.data(),prop.data(),ability.data(),unrelated.data()};
const char* identity="Fixture";
bool fault{},missing{};
template<class T> void set(unsigned char* p,unsigned at,T value){std::memcpy(p+at,&value,sizeof(value));}
template<class T> T get(const unsigned char* p,unsigned at){T value;std::memcpy(&value,p+at,sizeof(value));return value;}
bool subscribe(ToolInitializationCallback value) noexcept {callback=value;return true;}
void* __cdecl data(unsigned handle,unsigned type){CHECK(handle==0x44 && type==0x28);return missing?nullptr:properties.data();}
const char* __fastcall text(void* self,void*,const char* key,void* fallback){
    CHECK(self==properties.data()+8 && !std::strcmp(key,"LiteralCrabFigure") && !fallback);
    if(fault)RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);
    return identity;
}
void put(unsigned char* to,const char* pattern){while(*pattern){while(*pattern==' ')++pattern;if(!*pattern)break;if(*pattern=='?'){*to++=0;pattern+=2;}else{char* next{};*to++=static_cast<unsigned char>(std::strtoul(pattern,&next,16));pattern=next;}}}
void jump(unsigned char* at,unsigned char op,const void* dest){at[0]=op;const auto rel=static_cast<int>(reinterpret_cast<std::uintptr_t>(dest)-reinterpret_cast<std::uintptr_t>(at+5));std::memcpy(at+1,&rel,4);FlushInstructionCache(GetCurrentProcess(),at,5);}
void reset(){
    for(auto* entry:entries)std::memset(entry,0,0x80);
    set(tools.data(),4,actor.data());set(actor.data(),4,0x44u);
    set(tools.data(),0x2BC,entries.data());set(tools.data(),0x2C0,4u);
    set(item.data(),0x14,"ITEM_TEST");set(prop.data(),0x14,"PROP_TEST");
    child.fill(0);set(child.data(),0xC,"Tool_Original");set(item.data(),4,&childPointer);set(item.data(),8,1u);
    set(ability.data(),0x14,"ABILITY_TEST");set(unrelated.data(),0x14,"OTHER");
    for(auto* entry:entries)entry[0x78]=4;
    identity="Fixture";fault=missing=false;
}
}
int main(){
    auto* image=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x1400000,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));CHECK(image);if(!image)return 1;
    const std::pair<unsigned,const char*> patterns[]={
        {0xC1EEE0,"83 EC 40 53 8B D9"},{0xC1EEE6,"0F B7 0D ?? ?? ?? ?? 8B 43 04 8B 40 04 56 57 51 50"},
        {0x283E50,"8B 54 24 08 8B 0D ?? ?? ?? ?? 8B 01 8B 40 10 6A 00 52"},
        {0x2CFA30,"8B 44 24 08 8B 54 24 04 50 52 E8"},{0x2CFA46,"C2 08 00"},
        {0xC1E2C3,"8B 0D ?? ?? ?? ??"},{0xC1E2C9,"6B FF 4C 03 79 54"},
        {0xC19E20,"51 83 79 5C 00 0F 95 C0 84 C0 74 20 8A 41 78 C0 E8 02 24 01 74 0E E8 85 FF FF FF 84 C0 0F 94 C0 84 C0 75 08 83 C4 04 E9 C4 FF FF FF 59 C3"}};
    const std::pair<unsigned,unsigned> calls[]={{0xC1EEF7,0x283E50},{0xC1EF2F,0x2CFAE0},{0xC1EF41,0x2CFA30},{0xC1EF57,0xC1EB50},{0x2CFA3A,0x2CF9C0}};
    const auto setup=[&](){for(const auto& [rva,bytes]:patterns)put(image+rva,bytes);for(const auto& [rva,to]:calls)jump(image+rva,0xE8,image+to);
        auto* type=reinterpret_cast<unsigned short*>(image+0x1300000);*type=0x28;set(image,0xC1EEE9,type);
        auto* slot=image+0x1300010;set(image,0xC1E2C5,slot);set(slot,0,nativeTable.data());
        set(nativeTable.data(),0x50,1u);set(nativeTable.data(),0x54,nativeRow.data());};
    setup();
    CustomFigure f;f.name="Fixture";f.gameplay={"AV_SOURCE","characters/source.dnax","ITEM_TEST","PROP_TEST","ABILITY_TEST",{}, {}};
    for(const auto& [rva,_]:patterns){image[rva]^=1;CHECK(!installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f}));image[rva]^=1;}
    for(const auto& [rva,_]:calls){image[rva]^=1;CHECK(!installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f}));image[rva]^=1;}
    CHECK(installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));CHECK(callback);
    jump(image+0x283E50,0xE9,reinterpret_cast<void*>(&data));jump(image+0x2CFA30,0xE9,reinterpret_cast<void*>(&text));
    // Run the original x86 native visibility predicate, including its awarded
    // and suppression helpers, against the private inventory fixture.
    put(image+0xC19E10,"8A 41 78 D0 E8 24 01 C3");put(image+0xC19DC0,"8A 41 78 24 01 C3");
    using Visible=bool(__thiscall*)(void*);
    const auto visible=reinterpret_cast<Visible>(image+0xC19E20);
    reset();const auto originalAbility=ability,originalOther=unrelated;
    CHECK(!visible(item.data()) && !visible(prop.data()));
    callback(tools.data());CHECK(visible(item.data()) && visible(prop.data()));
    CHECK(ability==originalAbility && unrelated==originalOther);CHECK(item[0x78]==4 && prop[0x78]==4);
    // A locked item and a suppressed item retain the native restrictions.
    item[0x78]=0;CHECK(!visible(item.data()));item[0x78]=5;CHECK(!visible(item.data()));
    for(const char* other:{static_cast<const char*>(nullptr),"Unknown","Fixture_extra"}){reset();identity=other;const auto before=item;callback(tools.data());CHECK(item==before);}
    reset();missing=true;callback(tools.data());CHECK(!visible(item.data()));
    reset();fault=true;callback(tools.data());CHECK(!visible(item.data()));
    reset();set(tools.data(),0x2C0,513u);callback(tools.data());CHECK(!visible(item.data()));
    reset();set(prop.data(),0x14,"ITEM_TEST");callback(tools.data());CHECK(!visible(item.data()));
    // Control templates change only the selected private tool instance. The
    // original row and its native actor/script/inventory fields stay unchanged.
    removeCustomLoadouts();setup();
    f.gameplay.controlSources[0]="ITEM_CONTROL";f.gameplay.controlRows[0]=0;f.gameplay.controlHashes[0]=0x12345678;
    set(nativeRow.data(),4,0x12345678u);set(nativeRow.data(),0x24,0x1230u);
    set(nativeRow.data(),0x28,0x4560u);set(nativeRow.data(),0x30,0x7890u);
    const auto originalRow=nativeRow;
    CHECK(installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));
    jump(image+0x283E50,0xE9,reinterpret_cast<void*>(&data));jump(image+0x2CFA30,0xE9,reinterpret_cast<void*>(&text));
    reset();callback(tools.data());
    CHECK(get<unsigned>(item.data(),0x38)==0x1230 && get<unsigned>(item.data(),0x44)==0x4560 && get<unsigned>(item.data(),0x40)==0x7890);
    CHECK(get<unsigned>(prop.data(),0x38)==0 && get<unsigned>(item.data(),0x3C)==0 && nativeRow==originalRow);
    reset();set(nativeRow.data(),4,0u);callback(tools.data());CHECK(!visible(item.data()));nativeRow=originalRow;
    removeCustomLoadouts();setup();f.gameplay.itemWeapon.source="OriginalWeapon";
    f.gameplay.itemWeapon.sourceActor="Tool_Original";f.gameplay.itemWeapon.actor="lcr_private_item";
    CHECK(installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));
    jump(image+0x283E50,0xE9,reinterpret_cast<void*>(&data));jump(image+0x2CFA30,0xE9,reinterpret_cast<void*>(&text));
    reset();callback(tools.data());CHECK(std::string(get<const char*>(child.data(),0xC))=="lcr_private_item");
    reset();set(child.data(),0x5C,1u);callback(tools.data());CHECK(std::string(get<const char*>(child.data(),0xC))=="Tool_Original" && !visible(item.data()));
    removeCustomLoadouts();setup();CHECK(MH_Initialize()==MH_OK);
    CHECK(installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f}));
    jump(image+0x283E50,0xE9,reinterpret_cast<void*>(&data));jump(image+0x2CFA30,0xE9,reinterpret_cast<void*>(&text));
    // The trampoline executes the copied six-byte prologue, then a matching
    // stack cleanup standing in for the rest of the engine initializer.
    put(image+0xC1EEE6,"83 C4 40 5B C3");
    FlushInstructionCache(GetCurrentProcess(),image,0x1400000);
    reset();reinterpret_cast<void(__thiscall*)(void*)>(image+0xC1EEE0)(tools.data());
    CHECK(get<unsigned>(item.data(),0x5C)==1 && get<unsigned>(prop.data(),0x5C)==1);
    CHECK(get<unsigned>(ability.data(),0x5C)==0 && unrelated[0x78]==4);
    removeCustomLoadouts();setup();
    // Hook the real x86 profile ABI. Its original trampoline returns a stock
    // record; the private stream must apply only to this DNA marker and table.
    const std::pair<unsigned,const char*> profilePatterns[]={
        {0xB70690,"0F B7 05 ?? ?? ?? ?? 53 56 8B F1 8B 4C 24 0C"},
        {0xB706BD,"39 7E 58"},{0xB706C2,"8B 46 5C 8B 0C F8"},
        {0xC1F7E1,"8B 40 04 8B 18 83 C0 04"}};
    const auto profileSetup=[&](){setup();for(const auto& [rva,bytes]:profilePatterns)put(image+rva,bytes);
        jump(image+0xB706A5,0xE8,image+0x283E50);jump(image+0xC1F7D4,0xE8,image+0xB70690);
        set(image,0xB70693,reinterpret_cast<unsigned short*>(image+0x1300000));};
    profileSetup();f.gameplay.inventorySource="AV_Buzz";f.gameplay.inventoryRows={0,~0u};f.gameplay.inventoryHashes={0x12345678};
    for(const auto& [rva,_]:profilePatterns){image[rva]^=1;CHECK(!installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));image[rva]^=1;}
    for(unsigned rva:{0xB706A5u,0xC1F7D4u}){image[rva]^=1;CHECK(!installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));image[rva]^=1;}
    CHECK(installCustomLoadouts(reinterpret_cast<HMODULE>(image),{f},subscribe));
    std::array<unsigned,2> stockRows{0,~0u};std::array<const void*,2> stockProfile{"native",stockRows.data()};
    put(image+0xB70697,"B8 00 00 00 00 C2 04 00");set(image,0xB70698,stockProfile.data());
    jump(image+0x283E50,0xE9,reinterpret_cast<void*>(&data));jump(image+0x2CFA30,0xE9,reinterpret_cast<void*>(&text));
    FlushInstructionCache(GetCurrentProcess(),image,0x1400000);
    using Profile=const void*(__thiscall*)(void*,void*);
    const auto profile=reinterpret_cast<Profile>(image+0xB70690);
    reset();const auto stockBefore=stockProfile;const auto rowsBefore=stockRows;
    const auto* custom=profile(nativeTable.data(),actor.data());CHECK(custom && custom!=stockProfile.data());
    if(custom && custom!=stockProfile.data()) {const auto* rows=get<const unsigned*>(static_cast<const unsigned char*>(custom),4);CHECK(rows && rows[0]==0 && rows[1]==~0u);}
    for(const char* other:{static_cast<const char*>(nullptr),"Unknown","Fixture_extra"}){reset();identity=other;CHECK(profile(nativeTable.data(),actor.data())==stockProfile.data());}
    reset();missing=true;CHECK(profile(nativeTable.data(),actor.data())==stockProfile.data());
    reset();fault=true;CHECK(profile(nativeTable.data(),actor.data())==stockProfile.data());
    reset();CHECK(profile(nativeTable.data(),nullptr)==stockProfile.data());
    set(nativeRow.data(),4,0u);CHECK(profile(nativeTable.data(),actor.data())==stockProfile.data());nativeRow=originalRow;
    set(nativeTable.data(),0x50,0u);CHECK(profile(nativeTable.data(),actor.data())==stockProfile.data());set(nativeTable.data(),0x50,1u);
    CHECK(nativeRow==originalRow && stockRows==rowsBefore && stockProfile==stockBefore);
    removeCustomLoadouts();CHECK(MH_Uninitialize()==MH_OK);CHECK(VirtualFree(image,0,MEM_RELEASE));
    std::printf("Private loadout profile/ABI/native visibility checks: %u failure(s).\n",failures);return failures?1:0;
}
