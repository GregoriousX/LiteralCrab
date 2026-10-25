// SPDX-License-Identifier: GPL-3.0-only
// Startup-only audio declarations for private characters and tools. Files are
// retained as immutable bytes; no native sound bank needs to be authored.
#pragma once
#include "di1/custom_tools.hpp"
#include <memory>

namespace crabe::di1 {
struct AudioVariant {
    std::string event; // Exact existing native sound event, or empty for a file.
    std::shared_ptr<const std::string> bytes;
    std::string source; // Mod-relative file name or event:<native event>.
};
struct AudioCue {
    std::string trigger; // Actor message, Spawn, Idle, or the native event to replace.
    bool replace{};
    float volume{1}, chance{1};
    double cooldown{};
    // Native FEV defaults: 3D inverse rolloff, effects min 5, dialogue min 60.
    bool spatial{true};
    float minDistance{5}, maxDistance{10000};
    std::vector<AudioVariant> variants;
};
struct AudioProfile {
    std::string identity, dna, marker, actor;
    std::uint32_t sku{};
    bool tool{}, voice{};
    std::vector<std::string> banks;
    std::vector<AudioCue> cues;
    double idleInterval{30};
};

// Input mods are already enabled by Index::load, and figures/tools have already
// survived private-content preparation. Audio errors reject only that owner's
// audio, atomically across its effects and voice sections. Original data and
// character/tool declarations are never modified. Missing audio sections do
// nothing. Loose files are bounded WAV or Ogg Vorbis, never URLs or symlinks.
std::vector<AudioProfile> loadCustomAudio(const std::vector<mods::Mod>& mods,
    const std::vector<CustomFigure>& figures, const std::vector<CustomTool>& tools,
    std::vector<std::string>& messages);
}
