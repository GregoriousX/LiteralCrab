// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>

namespace crabe::di1 {
struct LanguageChoice {
    std::string name, dictionary;
    bool enabled{}, valid = true, factory{};
};
struct LanguageDefinition {
    const char* name;
    const char* dictionary;
    bool factory;
};
// DictionaryFile values from PS3 locale.gfg (including the Japanese update).
// Regional names are engine IDs; their dictionary stems are not lowercased IDs.
inline constexpr LanguageDefinition languageDefinitions[] = {
    {"EnglishNTSC", "english_ntsc", true}, {"EnglishPAL", "english_pal", false},
    {"FrenchNTSC", "frenchcanada", false}, {"FrenchPAL", "frenchparisian", true},
    {"Italian", "italian", false}, {"German", "german", true},
    {"SpanishNTSC", "spanishlatinamerica", true}, {"SpanishPAL", "castilianspanish", false},
    {"Japanese", "japanese", false}, {"Swedish", "swedish", false},
    {"Dutch", "dutch", false}, {"Danish", "danish", false}, {"Norwegian", "norwegian", false},
    {"Polish", "polish", false}, {"Russian", "russian", false}, {"Arabic", "arabic", false},
    {"Finnish", "finnish", false}, {"BrazilianPortuguese", "brazilianportuguese", true}
};
inline std::string lowercaseLanguage(std::string_view name) {
    std::string folded(name);
    for (auto& ch : folded) if (ch >= 'A' && ch <= 'Z') ch += 32;
    return folded;
}
inline LanguageChoice languageChoice(std::wstring_view setting) {
    while (!setting.empty() && (setting.front() == L' ' || setting.front() == L'\t')) setting.remove_prefix(1);
    while (!setting.empty() && (setting.back() == L' ' || setting.back() == L'\t')) setting.remove_suffix(1);
    LanguageChoice choice;
    if (setting.size() > 63) { choice.valid = false; return choice; }
    std::string lower;
    for (const auto ch : setting) {
        if ((ch < L'A' || ch > L'Z') && (ch < L'a' || ch > L'z')) { choice.valid = false; return choice; }
        choice.name += static_cast<char>(ch);
        lower += static_cast<char>(ch >= L'A' && ch <= L'Z' ? ch + 32 : ch);
    }
    if (lower.empty() || lower == "off" || lower == "default" || lower == "auto") {
        choice.name.clear(); return choice;
    }
    choice.enabled = true;
    struct Alias { const char* alias; const char* name; };
    for (const auto [alias, name] : {Alias{"english", "EnglishNTSC"}, {"french", "FrenchPAL"},
        {"spanish", "SpanishNTSC"}, {"portuguese", "BrazilianPortuguese"},
        {"englishus", "EnglishNTSC"}, {"englishuk", "EnglishPAL"},
        {"frenchcanadian", "FrenchNTSC"}, {"frenchcanada", "FrenchNTSC"},
        {"frenchparisian", "FrenchPAL"}, {"spanishlatinamerica", "SpanishNTSC"},
        {"spanishspain", "SpanishPAL"}, {"castilianspanish", "SpanishPAL"}})
        if (lower == alias) choice.name = name;
    lower = lowercaseLanguage(choice.name);
    for (const auto& language : languageDefinitions) {
        if (lower == lowercaseLanguage(language.name)) {
            choice.name = language.name;
            choice.dictionary = language.dictionary;
            choice.factory = language.factory;
            return choice;
        }
    }
    choice.dictionary = lowercaseLanguage(choice.name);
    return choice;
}
}
