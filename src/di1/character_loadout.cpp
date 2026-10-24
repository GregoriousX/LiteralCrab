// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_loadout.hpp"
#include "di1/common.hpp"
#include "minhook/MinHook.h"
#include <array>
#include <algorithm>
#include <cstring>

namespace crabe::di1 {
namespace {
using Initialize = void (__thiscall*)(void*);
using Data = void* (__cdecl*)(std::uint32_t,std::uint32_t);
using Text = const char* (__thiscall*)(void*,const char*,void*);
using Profile = const void* (__thiscall*)(void*,void*);
Initialize original{};
Profile originalProfile{};
Data data{}; Text text{};
const std::uint16_t* type{};
const unsigned char* const* nativeTable{};
void* ownedHook{};
void* profileHook{};
struct NativeProfile { const void* key{}; const std::uint32_t* rows{}; };
static_assert(sizeof(NativeProfile)==8);
struct Definition { CustomFigure figure; NativeProfile inventory; };
std::vector<Definition> definitions;
unsigned events{};

bool equal(const char* value,const std::string& expected) noexcept {
    if(!value) return false;
    for(std::size_t i=0;i<expected.size();++i) if(value[i]!=expected[i]) return false;
    return value[expected.size()]=='\0';
}
const void* __fastcall privateProfile(void* self,void*,void* actor) {
    const void* fallback=originalProfile(self,actor);
    __try {
        if(!self || !actor) return fallback;
        const auto handle=*reinterpret_cast<const std::uint32_t*>(static_cast<const unsigned char*>(actor)+4);
        auto* properties=static_cast<unsigned char*>(data(handle,*type));
        if(!properties) return fallback;
        const auto* identity=text(properties+8,"LiteralCrabFigure",nullptr);
        for(const auto& definition:definitions) {
            const auto& f=definition.figure;
            if(f.gameplay.inventoryRows.empty() || !equal(identity,f.name)) continue;
            const auto& rows=f.gameplay.inventoryRows;
            const auto& hashes=f.gameplay.inventoryHashes;
            if(rows.size()<2 || rows.size()!=hashes.size()+1 || rows.back()!=~0u || hashes.size()>512) return fallback;
            const auto* memory=static_cast<const unsigned char*>(self);
            const auto count=*reinterpret_cast<const unsigned*>(memory+0x50);
            const auto* tools=*reinterpret_cast<const unsigned char* const*>(memory+0x54);
            if(!tools) return fallback;
            for(std::size_t i=0;i<hashes.size();++i)
                if(rows[i]>=count || *reinterpret_cast<const unsigned*>(tools+rows[i]*76+4)!=hashes[i]) return fallback;
            // InitTools reads only +4 (the index stream) of this native record.
            // It constructs normal entries and evaluates the original per-actor
            // upgrades/world/suppression rules; no shared profile is rewritten.
            if(events++<64) log("Custom gameplay %s: private native inventory has %u tools.",f.name.c_str(),static_cast<unsigned>(hashes.size()));
            return &definition.inventory;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    return fallback;
}
void prepare(void* tools) noexcept {
    __try {
        if(!tools) return;
        auto* memory=static_cast<unsigned char*>(tools);
        const auto* actor=*reinterpret_cast<const unsigned char* const*>(memory+4);
        if(!actor) return;
        const auto handle=*reinterpret_cast<const std::uint32_t*>(actor+4);
        auto* properties=static_cast<unsigned char*>(data(handle,*type));
        if(!properties) return;
        const char* identity=text(properties+8,"LiteralCrabFigure",nullptr);
        if(!identity) return;
        for(const auto& definition:definitions) {
            const auto& f=definition.figure;
            if(!equal(identity,f.name)) continue;
            const auto count=*reinterpret_cast<const unsigned*>(memory+0x2C0);
            auto* const* entries=*reinterpret_cast<unsigned char* const* const*>(memory+0x2BC);
            if(!entries || count>512) return;
            unsigned changed{};
            unsigned slot{};
            for(const auto* wanted:{&f.gameplay.item,&f.gameplay.prop}) {
                const auto controlSlot=slot++;
                if(wanted->empty()) continue;
                unsigned char* found{};
                for(unsigned i=0;i<count;++i) {
                    auto* entry=entries[i];
                    if(!entry || !equal(*reinterpret_cast<const char* const*>(entry+0x14),*wanted)) continue;
                    if(found) return; // Ambiguous native inventory: leave this slot alone.
                    found=entry;
                }
                if(!found) continue;
                const auto& weapon=f.gameplay.itemWeapon;
                if(controlSlot==0 && weapon.enabled()) {
                    const auto childCount=*reinterpret_cast<const unsigned*>(found+8);
                    auto* const* children=*reinterpret_cast<unsigned char* const* const*>(found+4);
                    if(childCount!=1 || !children || !children[0]) return;
                    auto* child=children[0];
                    const auto* source=*reinterpret_cast<const char* const*>(child+0xC);
                    if(equal(source,weapon.sourceActor)) {
                        // Creation has not started yet. Redirect this private
                        // inventory child to a cloned Tool actor/DNA/script.
                        if(*reinterpret_cast<const unsigned*>(child+0x5C)) return;
                        *reinterpret_cast<const char**>(child+0xC)=weapon.actor.c_str();
                    } else if(!equal(source,weapon.actor)) return;
                }
                const auto controlRow=f.gameplay.controlRows[controlSlot];
                if(controlRow!=~0u) {
                    const auto* table=*nativeTable;
                    if(!table || controlRow>=*reinterpret_cast<const unsigned*>(table+0x50)) return;
                    const auto* rows=*reinterpret_cast<const unsigned char* const*>(table+0x54);
                    if(!rows) return;
                    const auto* source=rows+static_cast<std::size_t>(controlRow)*76;
                    if(*reinterpret_cast<const unsigned*>(source+4)!=f.gameplay.controlHashes[controlSlot]) return;
                    // Native lists are already relocated by the game's table loader.
                    // Borrow only targeting/behavior, control contexts and HUD hints;
                    // the tool's actors, projectile script and inventory stay its own.
                    for(const auto [to,from]:std::array<std::pair<unsigned,unsigned>,3>{{{0x38,0x24},{0x44,0x28},{0x40,0x30}}})
                        std::memcpy(found+to,source+from,4);
                }
                // Native GetNextTool uses +5C to omit NPC-only tool rows. This
                // is per-instance state; the shared binary table stays intact.
                *reinterpret_cast<unsigned*>(found+0x5C)=1;
                ++changed;
            }
            if(events++<64) log("Custom gameplay %s: %u declared item/prop slot(s) selectable on private avatar.",f.name.c_str(),changed);
            return;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {
        if(events++<64) log("Custom gameplay: private loadout initialization unavailable.");
    }
}
void __fastcall initialize(void* self,void*) { prepare(self);original(self); }
}
bool installCustomLoadouts(HMODULE host,const std::vector<CustomFigure>& figures,ToolInitializationRegistrar existing) {
    definitions.clear();
    for(const auto& f:figures) if(f.gameplay.enabled()) definitions.push_back({f,{}});
    for(auto& definition:definitions) definition.inventory.rows=definition.figure.gameplay.inventoryRows.data();
    if(definitions.empty()) return true;
    const auto base=reinterpret_cast<std::uintptr_t>(host);
    if(!codeMatches(base+0xC1EEE6,"0F B7 0D ?? ?? ?? ?? 8B 43 04 8B 40 04 56 57 51 50") ||
       !callsTo(base,0xC1EEF7,0x283E50) || !callsTo(base,0xC1EF2F,0x2CFAE0) ||
       !callsTo(base,0xC1EF41,0x2CFA30) || !callsTo(base,0xC1EF57,0xC1EB50) ||
       !codeMatches(base+0x283E50,"8B 54 24 08 8B 0D ?? ?? ?? ?? 8B 01 8B 40 10 6A 00 52") ||
       !codeMatches(base+0x2CFA30,"8B 44 24 08 8B 54 24 04 50 52 E8") ||
       !callsTo(base,0x2CFA3A,0x2CF9C0) || !codeMatches(base+0x2CFA46,"C2 08 00") ||
       !codeMatches(base+0xC19E20,"51 83 79 5C 00 0F 95 C0 84 C0") ||
       !codeMatches(base+0xC19E4C,"59 C3")) { definitions.clear();return false; }
    if(!codeMatches(base+0xC1E2C3,"8B 0D ?? ?? ?? ??") ||
       !codeMatches(base+0xC1E2C9,"6B FF 4C 03 79 54")) { definitions.clear();return false; }
    type=*reinterpret_cast<const std::uint16_t* const*>(base+0xC1EEE9);
    nativeTable=*reinterpret_cast<const unsigned char* const* const*>(base+0xC1E2C5);
    if(!readable(type,sizeof(*type)) || !readable(nativeTable,sizeof(*nativeTable))) { definitions.clear();return false; }
    data=reinterpret_cast<Data>(base+0x283E50);text=reinterpret_cast<Text>(base+0x2CFA30);
    const bool inventory=std::any_of(definitions.begin(),definitions.end(),[](const auto& d){return !d.figure.gameplay.inventoryRows.empty();});
    if(inventory) {
        if(!codeMatches(base+0xB70690,"0F B7 05 ?? ?? ?? ?? 53 56 8B F1 8B 4C 24 0C") ||
           !callsTo(base,0xB706A5,0x283E50) || !codeMatches(base+0xB706BD,"39 7E 58") ||
           !codeMatches(base+0xB706C2,"8B 46 5C 8B 0C F8") || !callsTo(base,0xC1F7D4,0xB70690) ||
           !codeMatches(base+0xC1F7E1,"8B 40 04 8B 18 83 C0 04")) { definitions.clear();return false; }
        void* trampoline{}; auto* target=reinterpret_cast<void*>(base+0xB70690);
        if(MH_CreateHook(target,reinterpret_cast<void*>(&privateProfile),&trampoline)!=MH_OK) { definitions.clear();return false; }
        originalProfile=reinterpret_cast<Profile>(trampoline);
        if(MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target);definitions.clear();return false; }
        profileHook=target;
    }
    events=0;
    if(existing && existing(&prepare)) return true;
    if(!codeMatches(base+0xC1EEE0,"83 EC 40 53 8B D9")) { removeCustomLoadouts();return false; }
    void* trampoline{};auto* target=reinterpret_cast<void*>(base+0xC1EEE0);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&initialize),&trampoline)!=MH_OK) { removeCustomLoadouts();return false; }
    original=reinterpret_cast<Initialize>(trampoline);
    if(MH_EnableHook(target)!=MH_OK) { MH_RemoveHook(target);removeCustomLoadouts();return false; }
    ownedHook=target;
    log("Mods: private custom character loadout initialization available.");
    return true;
}
void removeCustomLoadouts() noexcept {
    if(profileHook) { MH_DisableHook(profileHook);MH_RemoveHook(profileHook);profileHook=nullptr; }
    if(ownedHook) { MH_DisableHook(ownedHook);MH_RemoveHook(ownedHook);ownedHook=nullptr; }
    definitions.clear();
}
}
