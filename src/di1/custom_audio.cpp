// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_audio.hpp"
#include <algorithm>
#include <cmath>

namespace crabe::di1 {
std::string customAudioMarker(std::size_t profile, std::size_t cue) {
    return "LiteralCrabAudio:" + std::to_string(profile) + ":" + std::to_string(cue);
}
std::string customAudioMessage(const AudioProfile& profile, const AudioCue& cue) {
    auto trigger=cue.trigger;
    if (profile.tool && trigger=="Fire") trigger="ShootGun";
    if (profile.tool && (trigger=="Equip" || trigger=="Spawn")) trigger="Activate";
    if (profile.tool && trigger=="Unequip") trigger="Deactivate";
    if (!profile.tool && trigger=="Land") trigger="CollisionLanded";
    if (!profile.tool && trigger=="Respawn") trigger="DynamicExploderReassembled";
    return trigger;
}
std::string customAudioDnaPatch(const AudioProfile& profile, std::size_t index) {
    std::string code = "local _,_,log=...\nlocal entries={\n";
    for (std::size_t i = 0; i < profile.cues.size(); ++i) {
        const auto& cue = profile.cues[i];
        if (cue.replace || cue.trigger == "Idle" || (cue.trigger == "Spawn" && !profile.tool)) continue;
        const auto trigger=customAudioMessage(profile,cue);
        code += "{Type='SOUND',Event=" + mods::luaQuote(trigger) + ",Name=" +
            mods::luaQuote(customAudioMarker(index, i)) + "},\n";
    }
    code += R"lua(}
if #entries==0 then return end
if type(DNA)~='table' or type(DNA.AgentGadgets)~='table' then
    log('SKIPPED: private audio DNA contract differs');return
end
local gadgets=DNA.AgentGadgets
if gadgets.Effects~=nil and type(gadgets.Effects)~='table' then
    log('SKIPPED: private Effects gadget differs');return
end
local effects=gadgets.Effects or {}
if effects.FX~=nil and type(effects.FX)~='table' then
    log('SKIPPED: private Effects.FX differs');return
end
local rows=effects.FX or {}
for _,entry in ipairs(entries) do
    local present=false
    for _,old in ipairs(rows) do
        if type(old)=='table' and old.Name==entry.Name then present=true end
    end
    if not present then rows[#rows+1]=entry end
end
effects.FX=rows;gadgets.Effects=effects
log('APPLIED: private gameplay audio messages')
)lua";
    return code;
}
std::string customAudioBankPatch(const std::vector<AudioProfile>& profiles) {
    std::string code="local _,_,log=...\nlocal declarations={\n";
    for (const auto& profile : profiles) if (!profile.banks.empty()) {
        auto actor=profile.actor;
        for (auto& ch : actor) if (ch>='A' && ch<='Z') ch=static_cast<char>(ch-'A'+'a');
        code += "{actor=" + mods::luaQuote(actor) + ",files={";
        for (const auto& bank : profile.banks) code += mods::luaQuote("Audio|" + bank) + ",";
        code += "}},\n";
    }
    code += R"lua(}
if type(DataMap)~='table' or type(DataMap.Actor)~='table' then return end
for _,entry in ipairs(declarations) do
    local row=DataMap.Actor[entry.actor]
    if type(row)=='table' and type(row.Files)=='table' then
        for _,file in ipairs(entry.files) do
            local present=false
            for _,old in ipairs(row.Files) do
                if type(old)=='string' and string.lower(old)==string.lower(file) then present=true end
            end
            if not present then row.Files[#row.Files+1]=file end
        end
    end
end
)lua";
    return code;
}
void addCustomAudioPatches(mods::Index& index, const std::vector<AudioProfile>& profiles, bool nativeMessages) {
    for (std::size_t i = 0; !nativeMessages && i < profiles.size(); ++i) {
        const auto& profile = profiles[i];
        index.addBuiltinPatch({profile.dna,profile.marker,"contains:"+profile.marker},
            "custom_audio_"+std::to_string(i),customAudioDnaPatch(profile,i),"{}");
    }
    if (!profiles.empty()) index.addBuiltinPatch({"coredatamap.lua","rumpusroomdatamap.lua",
        "toystoryinspacedatamap.lua","incrediblesdatamap.lua","piratesdatamap.lua",
        "monstersudatamap.lua","carsdatamap.lua","lonerangerdatamap.lua","contains:DataMap =","contains:AnimTreeProject"},
        "custom_audio_banks",customAudioBankPatch(profiles),"{}");
}

CustomAudioPolicy::State* CustomAudioPolicy::state(std::size_t profile, std::uint32_t owner) noexcept {
    if (!owner) return nullptr;
    for (auto& value : states_) if (value.owner==owner && value.profile==profile) return &value;
    for (auto& value : states_) if (!value.owner) {
        value={};value.owner=owner;value.profile=profile;
        value.previous.fill(0xFF);
        return &value;
    }
    return nullptr;
}
std::uint32_t CustomAudioPolicy::random() noexcept {
    random_^=random_<<13;random_^=random_>>17;random_^=random_<<5;return random_;
}
std::optional<std::size_t> CustomAudioPolicy::choose(const std::vector<AudioProfile>& profiles,
    std::size_t profile, std::size_t cue, std::uint32_t owner, double now, bool gameplay) {
    if (!gameplay || !std::isfinite(now) || now<0 || profile>=profiles.size() ||
        cue>=profiles[profile].cues.size() || cue>=64) return {};
    const auto& p=profiles[profile];const auto& c=p.cues[cue];
    if (c.variants.empty() || c.variants.size()>8 || !std::isfinite(c.cooldown) || c.cooldown<0 ||
        !std::isfinite(c.chance) || c.chance<0 || c.chance>1) return {};
    auto* s=state(profile,owner);
    if (!s || now<s->next[cue]) return {};
    if (p.voice) for (const auto& other : states_)
        if (other.owner==owner && now<other.nextVoice) return {};
    // Failed probability rolls also consume the cooldown: repeated fire/hit
    // packets must not turn a rare bark into a certainty in one frame.
    s->next[cue]=now+c.cooldown;
    if (c.chance==0 || (c.chance<1 && double(random())/4294967296.0>=c.chance)) return {};
    std::size_t selected=random()%c.variants.size();
    if (c.variants.size()>1 && selected==s->previous[cue]) selected=(selected+1+random()%(c.variants.size()-1))%c.variants.size();
    s->previous[cue]=static_cast<unsigned char>(selected);
    if (p.voice) s->nextVoice=now+c.cooldown;
    return selected;
}
bool CustomAudioPolicy::idle(std::size_t profile, std::uint32_t owner, double now, double interval) {
    if (!std::isfinite(now) || now<0 || !std::isfinite(interval) || interval<1) return false;
    auto* s=state(profile,owner);if (!s) return false;
    if (!s->idleStarted) {s->nextIdle=now+interval;s->idleStarted=true;return false;}
    if (now<s->nextIdle) return false;
    s->nextIdle=now+interval;return true;
}
bool CustomAudioPolicy::spawned(std::size_t profile, std::uint32_t owner) {
    auto* s=state(profile,owner);if (!s || s->spawned) return false;
    s->spawned=true;return true;
}
void CustomAudioPolicy::retain(const std::array<std::uint32_t,16>& owners) noexcept {
    for (auto& s : states_) if (s.owner && std::find(owners.begin(),owners.end(),s.owner)==owners.end()) s={};
}
void CustomAudioPolicy::clear() noexcept { for (auto& s : states_) s={}; }
}
