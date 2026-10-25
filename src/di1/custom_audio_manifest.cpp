// SPDX-License-Identifier: GPL-3.0-only
#include "di1/custom_audio_manifest.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <map>
#include <set>
#include <stdexcept>

namespace crabe::di1 {
namespace {
constexpr std::size_t maxClipBytes = 8u << 20, maxAudioBytes = 64u << 20;
constexpr std::size_t maxManifestBytes = 1u << 20;
std::string lower(std::string_view text) {
    std::string out(text);
    for (auto& c : out) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}
std::wstring lower(std::wstring text) {
    for (auto& c : text) { if (c == L'/') c = L'\\'; c = static_cast<wchar_t>(std::towlower(c)); }
    if (text.starts_with(L"\\\\?\\unc\\")) text = L"\\\\" + text.substr(8);
    else if (text.starts_with(L"\\\\?\\")) text.erase(0, 4);
    return text;
}
std::string_view trim(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r')) text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r')) text.remove_suffix(1);
    return text;
}
bool printable(std::string_view text, std::size_t limit) {
    return !text.empty() && text.size() <= limit && std::all_of(text.begin(), text.end(), [](unsigned char c) { return c >= 32 && c <= 126; });
}
bool resourcePath(std::string_view path) {
    if (!printable(path, 239) || path.front() == '/' || path.back() == '/') return false;
    if (path.find_first_of("\\:<>\"|?*,") != path.npos) return false;
    for (std::size_t at = 0; at < path.size();) {
        const auto end = std::min(path.find('/', at), path.size());
        const auto part = path.substr(at, end - at);
        if (part.empty() || part == "." || part == ".." || part.back() == '.' || part.back() == ' ') return false;
        const auto stem = lower(part.substr(0, part.find('.')));
        if (stem == "con" || stem == "prn" || stem == "aux" || stem == "nul" ||
            (stem.size() == 4 && (stem.starts_with("com") || stem.starts_with("lpt")) && stem[3] >= '1' && stem[3] <= '9')) return false;
        at = end + 1;
    }
    return true;
}
struct Handle {
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE v) : value(v) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};
std::wstring fullPath(const std::wstring& path) {
    const auto count = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
    if (!count || count > 32768) throw std::runtime_error("invalid audio mod folder");
    std::wstring out(count, L'\0');
    const auto written = GetFullPathNameW(path.c_str(), count, out.data(), nullptr);
    if (!written || written >= count) throw std::runtime_error("invalid audio mod folder");
    out.resize(written);
    // The opened file's final path uses long names. Expand 8.3 components
    // (for example a RUNNER~1 temp folder) so the alias check compares like with like.
    const auto longCount = GetLongPathNameW(out.c_str(), nullptr, 0);
    if (longCount && longCount <= 32768) {
        std::wstring expanded(longCount, L'\0');
        const auto longWritten = GetLongPathNameW(out.c_str(), expanded.data(), longCount);
        if (longWritten && longWritten < longCount) { expanded.resize(longWritten); out = std::move(expanded); }
    }
    return out;
}
void rejectLinks(const std::wstring& path) {
    // Check the root and every ancestor, not only the final file. The opened
    // file's final path is independently checked below to catch redirection.
    const std::filesystem::path parsed(path);
    auto current = parsed.root_path();
    for (const auto& part : parsed.relative_path()) {
        current /= part;
        Handle handle(CreateFileW(current.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        BY_HANDLE_FILE_INFORMATION info{};
        if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info) ||
            (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("audio path is missing or behind a link");
    }
}
std::string readFile(const std::wstring& root, std::string_view relative, std::size_t limit) {
    if (!resourcePath(relative)) throw std::runtime_error("audio file must be a relative path inside its mod: " + std::string(relative));
    auto file = fullPath(root);
    if (!file.empty() && file.back() != L'\\') file += L'\\';
    file.append(relative.begin(), relative.end());
    std::replace(file.begin(), file.end(), L'/', L'\\');
    rejectLinks(file);
    Handle handle(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info) ||
        (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY)) || info.nFileSizeHigh || info.nFileSizeLow > limit)
        throw std::runtime_error("audio file is unreadable, linked, or exceeds its size limit: " + std::string(relative));
    const auto count = GetFinalPathNameByHandleW(handle.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!count || count > 32768) throw std::runtime_error("cannot verify audio file location: " + std::string(relative));
    std::wstring actual(count, L'\0');
    const auto written = GetFinalPathNameByHandleW(handle.value, actual.data(), count, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!written || written >= count) throw std::runtime_error("cannot verify audio file location: " + std::string(relative));
    actual.resize(written);
    if (lower(actual) != lower(file)) throw std::runtime_error("audio file location resolves through an alias: " + std::string(relative));
    std::string bytes(info.nFileSizeLow, '\0');
    DWORD read{};
    if (!bytes.empty() && (!ReadFile(handle.value, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) || read != bytes.size()))
        throw std::runtime_error("cannot read audio file: " + std::string(relative));
    return bytes;
}
struct Entry { std::string key, value; };
using Entries = std::vector<Entry>;
std::array<Entries, 2> sections(std::string_view source, std::string_view owner) {
    if (source.starts_with("\xEF\xBB\xBF")) source.remove_prefix(3);
    if (source.find('\0') != source.npos) throw std::runtime_error("audio manifest must be text without NUL bytes");
    std::array<Entries, 2> result;
    std::array<std::set<std::string>, 2> seen;
    std::array<std::size_t, 2> sizes{};
    int selected = -1;
    for (std::size_t at = 0; at < source.size();) {
        const auto end = std::min(source.find('\n', at), source.size());
        auto line = trim(source.substr(at, end - at));
        at = end + 1;
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        if (line.front() == '[' && line.back() == ']') {
            const auto name = lower(trim(line.substr(1, line.size() - 2)));
            selected = name == std::string(owner) + ".audio" ? 0 : name == std::string(owner) + ".voice" ? 1 : -1;
            continue;
        }
        if (selected < 0) continue;
        sizes[selected] += line.size();
        if (sizes[selected] > 32768) throw std::runtime_error("an audio section exceeds 32 KiB");
        const auto split = line.find('=');
        if (split == line.npos) throw std::runtime_error("audio entry must have a key and value");
        const auto key = trim(line.substr(0, split));
        auto value = trim(line.substr(split + 1));
        if (value.size() >= 2 && ((value.front() == '"' && value.back() == '"') || (value.front() == '\'' && value.back() == '\''))) {
            value.remove_prefix(1); value.remove_suffix(1);
        }
        if (!printable(key, 255) || !printable(value, 32768)) throw std::runtime_error("audio keys and values must be nonempty printable ASCII");
        if (!seen[selected].insert(lower(key)).second) throw std::runtime_error("duplicate audio key: " + std::string(key));
        result[selected].push_back({std::string(key), std::string(value)});
    }
    return result;
}
std::vector<std::string> list(std::string_view text, std::size_t maximum) {
    std::vector<std::string> out;
    std::set<std::string> seen;
    for (std::size_t at = 0; at <= text.size();) {
        const auto end = std::min(text.find(',', at), text.size());
        const auto value = trim(text.substr(at, end - at));
        if (value.empty()) throw std::runtime_error("empty audio list entry");
        if (!seen.insert(lower(value)).second) throw std::runtime_error("duplicate audio list entry: " + std::string(value));
        out.emplace_back(value);
        if (out.size() > maximum) throw std::runtime_error("too many audio list entries (limit " + std::to_string(maximum) + ")");
        if (end == text.size()) break;
        at = end + 1;
    }
    return out;
}
double number(std::string_view text, double low, double high, std::string_view label) {
    const std::string value(text);
    char* end{};
    const auto out = std::strtod(value.c_str(), &end);
    if (end != value.c_str() + value.size() || end == value.c_str() || !std::isfinite(out) || out < low || out > high)
        throw std::runtime_error(std::string(label) + " must be in " + std::to_string(low) + ".." + std::to_string(high));
    return out;
}
struct Cache {
    std::map<std::wstring, std::shared_ptr<const std::string>> files;
    std::size_t bytes{};
};
AudioVariant variant(const std::wstring& mod, std::string source, Cache& cache) {
    AudioVariant result;
    result.source = std::move(source);
    if (lower(result.source).starts_with("event:")) {
        result.event = result.source.substr(6);
        if (!printable(result.event, 255)) throw std::runtime_error("native audio event must contain 1..255 printable ASCII bytes");
        return result;
    }
    const auto path = lower(result.source);
    if (!resourcePath(result.source) || (!path.ends_with(".ogg") && !path.ends_with(".wav")))
        throw std::runtime_error("audio source must be a mod-relative .ogg/.wav or event:<native event>: " + result.source);
    auto key = fullPath(mod) + L"\\" + std::wstring(result.source.begin(), result.source.end());
    key = lower(std::move(key));
    if (const auto found = cache.files.find(key); found != cache.files.end()) {
        result.bytes = found->second;
        return result;
    }
    auto bytes = readFile(mod, result.source, maxClipBytes);
    if (path.ends_with(".wav")) {
        if (bytes.size() < 12 || bytes.substr(0, 4) != "RIFF" || bytes.substr(8, 4) != "WAVE")
            throw std::runtime_error("audio WAV needs a RIFF/WAVE header: " + result.source);
    } else {
        const auto header = std::string_view(bytes).substr(0, std::min<std::size_t>(4096, bytes.size()));
        if (!bytes.starts_with("OggS") || header.find(std::string_view("\x01vorbis", 7)) == header.npos)
            throw std::runtime_error("audio OGG must contain Ogg Vorbis (Opus is not supported): " + result.source);
    }
    if (bytes.size() > maxAudioBytes - cache.bytes) throw std::runtime_error("custom audio exceeds 64 MiB of unique files");
    cache.bytes += bytes.size();
    result.bytes = std::make_shared<const std::string>(std::move(bytes));
    cache.files.emplace(std::move(key), result.bytes);
    return result;
}
bool cueName(std::string_view key) {
    return !key.empty() && key.size() <= 127 && std::all_of(key.begin(), key.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
            c == '_' || c == '-' || c == '.' || c == '/' || c == ':';
    });
}
// Per-cue suffixes, in override-array order, with their accepted ranges.
constexpr std::array<std::string_view, 6> cueSettings{"volume", "chance", "cooldown", "spatial", "mindistance", "maxdistance"};
constexpr std::array<std::pair<double, double>, 6> cueRanges{{{0, 1}, {0, 1}, {0, 3600}, {0, 1}, {0.1, 100000}, {0.1, 1000000}}};
std::size_t cueSetting(std::string_view suffix) {
    return static_cast<std::size_t>(std::find(cueSettings.begin(), cueSettings.end(), suffix) - cueSettings.begin());
}
bool flag(std::string_view text, std::string_view label) {
    if (text == "1") return true;
    if (text == "0") return false;
    throw std::runtime_error(std::string(label) + " must be 0 or 1");
}
AudioProfile profile(const Entries& fields, AudioProfile base, const std::wstring& mod, Cache& cache) {
    // Defaults mirror the shipped FEV events: character effects are 3D with a
    // 5-unit inverse-rolloff minimum, dialogue is 3D with a 60-unit minimum.
    std::array<double, 6> defaults{1, 1, base.voice ? 15 : 0.08, 1, base.voice ? 60.0 : 5.0, 10000};
    std::map<std::string, std::array<std::optional<double>, 6>> overrides;
    for (const auto& entry : fields) {
        const auto key = lower(entry.key);
        if (const auto setting = cueSetting(key); setting < cueSettings.size())
            defaults[setting] = setting == 3 ? flag(entry.value, entry.key) :
                number(entry.value, cueRanges[setting].first, cueRanges[setting].second, entry.key);
        else if (key == "idleinterval") base.idleInterval = number(entry.value, 1, 3600, entry.key);
        else if (key == "banks") {
            base.banks = list(entry.value, 64);
            for (auto& bank : base.banks) {
                if (!resourcePath(bank)) throw std::runtime_error("Banks entries must name relative native FEV resources");
                const auto slash = bank.find_last_of('/'), dot = bank.find_last_of('.');
                if (dot == bank.npos || (slash != bank.npos && dot < slash)) bank += ".fev";
                else if (!lower(bank).ends_with(".fev")) throw std::runtime_error("Banks entries must name native .fev resources");
            }
            std::set<std::string> distinct;
            for (const auto& bank : base.banks) if (!distinct.insert(lower(bank)).second) throw std::runtime_error("duplicate native FEV resource");
        } else {
            const auto dot = key.rfind('.');
            const auto index = dot == key.npos ? cueSettings.size() : cueSetting(key.substr(dot + 1));
            if (index < cueSettings.size())
                overrides[key.substr(0, dot)][index] = index == 3 ? flag(entry.value, entry.key) :
                    number(entry.value, cueRanges[index].first, cueRanges[index].second, entry.key);
        }
    }
    for (const auto& entry : fields) {
        const auto key = lower(entry.key);
        if (cueSetting(key) < cueSettings.size() || key == "idleinterval" || key == "banks") continue;
        const auto dot = key.rfind('.');
        if (dot != key.npos && cueSetting(key.substr(dot + 1)) < cueSettings.size()) continue;
        AudioCue cue;
        cue.replace = key.starts_with("replace.");
        cue.trigger = cue.replace ? entry.key.substr(8) : entry.key;
        if (!cueName(cue.trigger)) throw std::runtime_error("invalid audio trigger: " + entry.key);
        if (base.tool && !cue.replace && cue.trigger=="Idle")
            throw std::runtime_error("tool audio has no Idle timer; use Activate/Fire or put Idle in Character.Voice");
        auto values = defaults;
        if (const auto found = overrides.find(key); found != overrides.end()) {
            for (std::size_t i = 0; i < values.size(); ++i) if (found->second[i]) values[i] = *found->second[i];
            overrides.erase(found);
        }
        cue.volume = static_cast<float>(values[0]); cue.chance = static_cast<float>(values[1]); cue.cooldown = values[2];
        cue.spatial = values[3] != 0;
        cue.minDistance = static_cast<float>(values[4]); cue.maxDistance = static_cast<float>(values[5]);
        if (cue.maxDistance < cue.minDistance)
            throw std::runtime_error("audio MaxDistance must not be below MinDistance: " + entry.key);
        for (auto& source : list(entry.value, 8)) cue.variants.push_back(variant(mod, std::move(source), cache));
        base.cues.push_back(std::move(cue));
        if (base.cues.size() > 64) throw std::runtime_error("an audio profile supports at most 64 cues");
    }
    if (!overrides.empty()) throw std::runtime_error("per-cue audio setting has no matching cue: " + overrides.begin()->first);
    return base;
}
}

std::vector<AudioProfile> loadCustomAudio(const std::vector<mods::Mod>& mods,
    const std::vector<CustomFigure>& figures, const std::vector<CustomTool>& tools,
    std::vector<std::string>& messages) {
    std::vector<AudioProfile> result;
    Cache cache;
    for (const auto& mod : mods) for (const bool tool : {false, true}) {
        AudioProfile base;
        base.tool = tool;
        const auto owner = tool ? "tool" : "character";
        if (tool) {
            const auto found = std::find_if(tools.begin(), tools.end(), [&](const auto& t) { return lower(t.modPath) == lower(mod.path) && !t.privateDna.empty(); });
            if (found == tools.end()) continue;
            base.identity = found->table.id; base.actor = found->table.actor;
            base.dna = found->privateDna; base.marker = "LiteralCrabTool:" + base.identity;
        } else {
            const auto found = std::find_if(figures.begin(), figures.end(), [&](const auto& f) { return lower(f.modPath) == lower(mod.path) && f.gameplay.enabled(); });
            if (found == figures.end()) continue;
            base.identity = found->name; base.actor = found->actor; base.sku = found->sku;
            base.dna = "characters/literalcrab/" + lower(found->name) + ".dnax";
            base.marker = "LiteralCrabCustomDNA:" + base.identity;
        }
        try {
            const auto fields = sections(readFile(mod.path, "mod.ini", maxManifestBytes), owner);
            if (fields[0].empty() && fields[1].empty()) continue;
            auto pendingCache = cache;
            std::vector<AudioProfile> pending;
            for (unsigned voice = 0; voice < 2; ++voice) if (!fields[voice].empty()) {
                auto seed = base; seed.voice = voice != 0;
                auto prepared = profile(fields[voice], std::move(seed), mod.path, pendingCache);
                if (!prepared.cues.empty() || !prepared.banks.empty()) pending.push_back(std::move(prepared));
            }
            cache = std::move(pendingCache);
            for (auto& prepared : pending) result.push_back(std::move(prepared));
            messages.push_back("Custom audio " + base.identity + ": effects/voice declarations prepared privately");
        } catch (const std::exception& error) {
            messages.push_back("Custom audio " + base.identity + ": REJECTED: " + error.what());
        }
    }
    return result;
}
}
