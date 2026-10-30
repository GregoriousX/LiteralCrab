// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace crabe::di1 {
enum class CustomAudioBus { Effects, Voice };
using CustomAudioTick = void (*)() noexcept;
// The game feeds FMOD raw world coordinates: its listener is the camera's world
// position/orientation and FMOD runs right-handed with 3D scale 1. A source is
// therefore an actor's world position, with the native inverse rolloff.
struct CustomAudioSpace {
    float position[3]{};
    float minDistance{5}, maxDistance{10000};
};

// These calls belong to the game's audio-update thread. The tick callback is
// invoked there after a successful native FMOD EventSystem update. Playback
// reuses that system and its sfx/dialog category groups; it creates no output
// device and never modifies native events, categories, or cutscene audio.
bool installCustomAudioPlayback(HMODULE host, CustomAudioTick tick);
bool playCustomAudio(std::uint32_t owner, std::shared_ptr<const std::string> bytes,
    CustomAudioBus bus, float volume = 1.0f, const CustomAudioSpace* space = nullptr) noexcept;
void stopCustomAudio(std::uint32_t owner) noexcept;
void stopAllCustomAudio() noexcept;
void setCustomAudioPaused(bool paused) noexcept;
bool customAudioPlaybackSuspended() noexcept;
bool customAudioVoiceActive(std::uint32_t owner) noexcept;
void updateCustomAudioPosition(std::uint32_t owner,const float* position) noexcept;
// A native event a custom character or tool plays may belong to a bank the game loaded without
// its waves (a character picked mid-game gets its banks' events but not their samples). Queues
// the event's group: on the audio thread its sample data is loaded once, so the event and its
// group's other events (a gun's shot and impact) become audible. Unknown names are ignored.
void requestNativeEventData(const char* event) noexcept;
// Diagnostic read of FMOD listener 0, as last set by the game's camera.
bool customAudioListener(float (&position)[3]) noexcept;
void removeCustomAudioPlayback() noexcept;

namespace audio_playback_detail {
// FMOD Ex 4.44.14's x86 createSound copies exactly 0x22 words of EXINFO.
// Only cbsize and length are authored here; all optional fields remain zero.
// The representation deliberately does not invent names for unused ABI fields.
struct SoundInfo {
    std::uint32_t cbsize{}, length{};
    std::array<std::uint32_t, 32> unused{};
};
static_assert(sizeof(SoundInfo) == 0x88);
struct Vector { float x{}, y{}, z{}; };
struct Api {
    int (__stdcall* getSystem)(void*, void**){};
    int (__stdcall* getVersion)(void*, unsigned*){};
    int (__stdcall* getCategory)(void*, const char*, void**){};
    int (__stdcall* getCategoryGroup)(void*, void**){};
    int (__stdcall* createSound)(void*, const char*, unsigned, SoundInfo*, void**){};
    int (__stdcall* playSound)(void*, int, void*, int, void**){};
    int (__stdcall* setGroup)(void*, void*){};
    int (__stdcall* setPaused)(void*, int){};
    int (__stdcall* setVolume)(void*, float){};
    int (__stdcall* setPosition)(void*, const Vector*, const Vector*){};
    int (__stdcall* setDistance)(void*, float, float){};
    int (__stdcall* isPlaying)(void*, int*){};
    int (__stdcall* stop)(void*){};
    int (__stdcall* releaseSound)(void*){};
    int (__stdcall* groupPaused)(void*, int*){};
    int (__stdcall* parentGroup)(void*, void**){};
    int (__stdcall* listener)(void*, int, Vector*, Vector*, Vector*, Vector*){}; // Optional diagnostics.
    bool complete() const noexcept;
};

// The mixer owns only its sixteen channels/sounds. An injected API also lets
// tests verify release ordering and routing without opening a sound device.
class Mixer {
public:
    bool bind(void* events, const Api& api) noexcept;
    bool play(std::uint32_t owner, std::shared_ptr<const std::string> bytes,
        CustomAudioBus bus, float volume, const CustomAudioSpace* space) noexcept;
    void update() noexcept;
    bool listener(float (&position)[3]) const noexcept;
    void stopOwner(std::uint32_t owner) noexcept;
    void stopAll() noexcept;
    void pause(bool value) noexcept;
    void unbind() noexcept;
    std::size_t active() const noexcept;
    bool suspended() const noexcept;
    bool voiceActive(std::uint32_t owner) const noexcept;
    void move(std::uint32_t owner,const float* position) noexcept;
    void* events() const noexcept { return events_; }
private:
    struct Voice {
        std::uint32_t owner{};
        CustomAudioBus bus{CustomAudioBus::Effects};
        bool spatial{};
        void* sound{};
        void* channel{};
        std::shared_ptr<const std::string> bytes;
    };
    void clear(Voice& voice) noexcept;
    Api api_{};
    void* events_{};
    void* system_{};
    bool paused_{};
    std::array<Voice, 16> voices_{};
};

// The host-only import thunks are checked independently of the FMOD exports.
// A mapped image may be the live executable or a read-only test mapping.
bool validProfile(const unsigned char* image, std::size_t bytes, std::uintptr_t base) noexcept;
}
}
