// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "di1/custom_audio_manifest.hpp"
#include <array>
#include <optional>

namespace crabe::di1 {
// Compatibility fallback when native actor-message observation is unavailable.
// Only private DNA receives synthetic names, consumed before native lookup.
std::string customAudioMarker(std::size_t profile, std::size_t cue);
std::string customAudioDnaPatch(const AudioProfile& profile, std::size_t index);
std::string customAudioMessage(const AudioProfile& profile, const AudioCue& cue);
// A tool cloned from a native gun keeps that gun's shot event. Its effects Fire cue becomes the
// replacement of that event, so a shot plays only the mod's clip, also where the native bank is
// loaded without its waves. An explicit Replace of the same event wins. Returns cues converted.
std::size_t bindToolFireSounds(std::vector<AudioProfile>& profiles, const std::vector<CustomTool>& tools);
std::string customAudioBankPatch(const std::vector<AudioProfile>& profiles);
void addCustomAudioPatches(mods::Index& index, const std::vector<AudioProfile>& profiles, bool nativeMessages = false);
// Observe the game's existing actor dispatch without changing its result/data.
void setCustomAudioMessageRouting(bool enabled) noexcept;
void observeCustomAudioMessage(const char* message, std::uint32_t receiver) noexcept;

// Bounded, deterministic routing policy, independent of native engine objects.
// A blocked context is never queued. Idle clocks advance only during gameplay.
class CustomAudioPolicy {
public:
    explicit CustomAudioPolicy(std::uint32_t seed = 0xA17D1024) : random_(seed ? seed : 1) {}
    std::optional<std::size_t> choose(const std::vector<AudioProfile>& profiles,
        std::size_t profile, std::size_t cue, std::uint32_t owner, double now, bool gameplay);
    bool idle(std::size_t profile, std::uint32_t owner, double now, double interval);
    bool spawned(std::size_t profile, std::uint32_t owner);
    void retain(const std::array<std::uint32_t, 16>& owners) noexcept;
    void clear() noexcept;
private:
    struct State {
        std::uint32_t owner{};
        std::size_t profile{};
        std::array<double, 64> next{};
        std::array<unsigned char, 64> previous{};
        double nextIdle{};
        double nextVoice{};
        bool spawned{}, idleStarted{};
    };
    std::array<State, 64> states_{};
    std::uint32_t random_;
    State* state(std::size_t profile, std::uint32_t owner) noexcept;
    std::uint32_t random() noexcept;
};

// Installs only when a prepared private owner declares audio. Refusing a native
// profile leaves both the character/tool and every original sound unchanged.
bool installCustomAudio(HMODULE host, std::vector<AudioProfile> profiles,
    const std::vector<CustomFigure>& figures, const std::vector<CustomTool>& tools);
void removeCustomAudio() noexcept;
}
