// SPDX-License-Identifier: GPL-3.0-only
#include "mods/mod_index.hpp"
#include <windows.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <map>
#include <stdexcept>

namespace crabe::mods {
namespace {
    constexpr std::size_t maxMods = 256;
    constexpr std::size_t maxScripts = 4096;
    constexpr std::size_t maxScriptBytes = 4u << 20;

    // Wrapper composed around a game chunk after it compiles. Runs the chunk unchanged in the
    // environment the game gives it, then the patch with the chunk's first result; a patch error
    // is reported and never replaces the chunk's own results or errors. lib holds the engine's
    // own library functions, so patches also work in data-only Lua states without them.
    constexpr std::string_view builderTail = R"lua(
local getfenv, setfenv, setmetatable, pcall, tostring =
      lib.getfenv, lib.setfenv, lib.setmetatable, lib.pcall, lib.tostring
local function say(message) log(label .. ': ' .. tostring(message)) end
local function finish(environment, ...)
    -- Globals resolve in the chunk's environment first, then in lib; writes go to the chunk's.
    setfenv(patch, setmetatable({}, {
        __index = function(_, key)
            local value = environment[key]
            if value == nil then return lib[key] end
            return value
        end,
        __newindex = environment,
    }))
    local ok, detail = pcall(patch, (...), options, say, environment)
    if not ok then say('FAILED: ' .. tostring(detail)) end
    return ...
end
return function(...)
    local environment = getfenv(1)
    setfenv(original, environment)
    return finish(environment, original(...))
end
)lua";

    std::string utf8(std::wstring_view value) {
        if (value.empty()) return {};
        const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
        if (size <= 0) throw std::runtime_error("text is not valid Unicode");
        std::string result(static_cast<std::size_t>(size), '\0');
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(),
            static_cast<int>(value.size()), result.data(), size, nullptr, nullptr);
        return result;
    }

    using Entries = std::vector<std::pair<std::wstring, std::wstring>>;
    // Keeps duplicate keys and file order, unlike a map, so one mod may patch a chunk twice.
    Entries section(const wchar_t* name, const std::wstring& file) {
        std::vector<wchar_t> buffer(32768);
        const auto count = GetPrivateProfileSectionW(name, buffer.data(),
            static_cast<DWORD>(buffer.size()), file.c_str());
        if (count >= buffer.size() - 2) throw std::runtime_error("an INI section exceeds 32 KiB");
        Entries result;
        for (const wchar_t* entry = buffer.data(); *entry; entry += wcslen(entry) + 1) {
            if (*entry == L';' || *entry == L'#') continue;
            const std::wstring_view text(entry);
            const auto delimiter = text.find(L'=');
            if (delimiter == std::wstring_view::npos || !delimiter)
                throw std::runtime_error("invalid INI line: " + utf8(text));
            auto key = text.substr(0, delimiter), value = text.substr(delimiter + 1);
            while (!key.empty() && key.back() == L' ') key.remove_suffix(1);
            while (!value.empty() && value.front() == L' ') value.remove_prefix(1);
            result.emplace_back(key, value);
        }
        return result;
    }

    // Win32 file access keeps the ASI free of iostream/filesystem code; this runs once at startup.
    class Handle {
    public:
        explicit Handle(HANDLE value) : value(value) {}
        Handle(const Handle&) = delete;
        Handle& operator=(const Handle&) = delete;
        ~Handle() { if (valid()) CloseHandle(value); }
        bool valid() const { return value != INVALID_HANDLE_VALUE; }
        HANDLE get() const { return value; }
    private:
        HANDLE value;
    };

    Handle open(const std::wstring& path, bool directory) {
        return Handle(CreateFileW(path.c_str(), directory ? FILE_READ_ATTRIBUTES : GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            directory ? FILE_FLAG_BACKUP_SEMANTICS : FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    }

    // Resolves "..", short names, junctions and symbolic links, so containment is checked on
    // the file actually opened.
    std::wstring finalPath(HANDLE handle) {
        std::wstring path(MAX_PATH, L'\0');
        for (;;) {
            const auto length = GetFinalPathNameByHandleW(handle, path.data(), static_cast<DWORD>(path.size()), 0);
            if (!length) throw std::runtime_error("cannot resolve a mod path");
            if (length < path.size()) { path.resize(length); return path; }
            path.resize(length);
        }
    }

    bool isFile(const std::wstring& path) {
        const auto attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
    }

    std::string readScript(const std::wstring& folder, const std::wstring& resolvedFolder, const std::wstring& relative) {
        const auto dot = relative.rfind(L'.');
        if (dot == std::wstring::npos || relative.find_first_of(L"\\/", dot) != std::wstring::npos ||
            _wcsicmp(relative.c_str() + dot, L".lua"))
            throw std::runtime_error("script must be a .lua file: " + utf8(relative));
        const Handle file = open(folder + L'\\' + relative, false);
        if (!file.valid()) throw std::runtime_error("cannot open " + utf8(relative));
        const auto resolved = finalPath(file.get());
        if (resolved.size() <= resolvedFolder.size() + 1 || resolved[resolvedFolder.size()] != L'\\' ||
            _wcsnicmp(resolved.c_str(), resolvedFolder.c_str(), resolvedFolder.size()))
            throw std::runtime_error("script must be inside the mod folder: " + utf8(relative));
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.get(), &size) || size.QuadPart <= 0 || size.QuadPart > static_cast<LONGLONG>(maxScriptBytes))
            throw std::runtime_error("script size must be 1 byte to 4 MiB: " + utf8(relative));
        std::string source(static_cast<std::size_t>(size.QuadPart), '\0');
        DWORD read{};
        if (!ReadFile(file.get(), source.data(), static_cast<DWORD>(source.size()), &read, nullptr) || read != source.size())
            throw std::runtime_error("cannot read " + utf8(relative));
        // Source text only: precompiled chunks bypass the Lua verifier.
        if (source[0] == '\x1b' || source.find('\0') != std::string::npos)
            throw std::runtime_error("script must be Lua source text, not bytecode: " + utf8(relative));
        return source;
    }

    bool integer(std::wstring_view value) {
        if (!value.empty() && value.front() == L'-') value.remove_prefix(1);
        return !value.empty() && value.size() <= 9 &&
            std::all_of(value.begin(), value.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
    }

    // "contains:<marker>" selects unnamed chunks by their source text; anything else is a name.
    std::string targetKey(const std::wstring& key, bool& content) {
        content = key.size() > 9 && !_wcsnicmp(key.c_str(), L"contains:", 9);
        if (content) {
            auto marker = utf8(std::wstring_view(key).substr(9));
            if (marker.size() < minMarker) throw std::runtime_error("content marker shorter than 8 bytes: " + marker);
            return marker;
        }
        const auto text = utf8(key);
        char buffer[maxChunkName];
        const auto length = normalize(text, buffer);
        if (!length || buffer[length - 1] == '/') throw std::runtime_error("invalid chunk name: " + text);
        return std::string(buffer, length);
    }

    struct Pending {
        std::string target;
        bool content{};
        bool override{};
        Script script;
    };
}

std::string luaQuote(std::string_view text) {
    std::string result = "\"";
    for (const char ch : text) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte == '\\' || byte == '"') {
            result += '\\';
            result += ch;
        } else if (byte < 32 || byte == 127) {
            char escaped[5];
            std::snprintf(escaped, sizeof(escaped), "\\%03u", byte);
            result += escaped;
        } else {
            result += ch;
        }
    }
    result += '"';
    return result;
}

std::size_t normalize(std::string_view name, char (&out)[maxChunkName]) noexcept {
    if (!name.empty() && (name.front() == '@' || name.front() == '=')) name.remove_prefix(1);
    while (name.size() >= 2 && name[0] == '.' && (name[1] == '/' || name[1] == '\\')) name.remove_prefix(2);
    if (name.empty() || name.size() >= maxChunkName) return 0;
    for (std::size_t index = 0; index < name.size(); ++index) {
        const char ch = name[index];
        out[index] = ch == '\\' ? '/' : ch >= 'A' && ch <= 'Z' ? static_cast<char>(ch + 32) : ch;
    }
    return name.size();
}

std::string_view chunkIdentity(const char* data, std::size_t size, const char* name) noexcept {
    // Lua 5.1 header: ESC "Lua" 0x51, format, endianness, int, size_t, Instruction, lua_Number,
    // integral; then the main function's source as size_t length (with NUL) plus bytes.
    if (data && size > 16 && !std::memcmp(data, "\x1bLua\x51", 5) && data[6] == 1 && data[8] == 4) {
        std::uint32_t length{};
        std::memcpy(&length, data + 12, sizeof(length));
        if (length > 1 && length <= size - 16) return std::string_view(data + 16, length - 1);
    }
    return name ? std::string_view(name) : std::string_view();
}

std::size_t Index::find(std::string_view identity, std::string_view chunk, const Target* (&out)[maxMatches]) const noexcept {
    char buffer[maxChunkName];
    const auto length = normalize(identity, buffer);
    std::size_t count = 0;
    if (!length) {
        // Unnamed chunks only: named ones never pay for a content scan.
        for (auto content = contents.begin(); content != contents.end() && count < maxMatches; ++content)
            if (chunk.find(content->path) != std::string_view::npos) out[count++] = &*content;
        return count;
    }
    const std::string_view full(buffer, length);
    const auto slash = full.rfind('/');
    const auto base = slash == std::string_view::npos ? full : full.substr(slash + 1);
    auto first = std::lower_bound(targets.begin(), targets.end(), base,
        [](const Target& target, std::string_view value) { return target.name() < value; });
    for (; first != targets.end() && first->name() == base && count < maxMatches; ++first) {
        const std::string_view path = first->path;
        if (full.ends_with(path) && (full.size() == path.size() || full[full.size() - path.size() - 1] == '/'))
            out[count++] = &*first;
    }
    return count;
}

Index Index::load(const std::wstring& modsRoot, std::vector<std::string>& messages) {
    Index index;
    std::vector<std::wstring> folders;
    WIN32_FIND_DATAW entry{};
    const HANDLE search = FindFirstFileExW((modsRoot + L"\\*").c_str(), FindExInfoBasic, &entry,
        FindExSearchLimitToDirectories, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (search == INVALID_HANDLE_VALUE) return index;
    do {
        if ((entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && wcscmp(entry.cFileName, L".") &&
            wcscmp(entry.cFileName, L"..") && isFile(modsRoot + L'\\' + entry.cFileName + L"\\mod.ini"))
            folders.emplace_back(entry.cFileName);
    } while (FindNextFileW(search, &entry));
    FindClose(search);
    // Deterministic, locale-independent load order: folder names compared ordinally.
    std::sort(folders.begin(), folders.end(), [](const std::wstring& left, const std::wstring& right) {
        return _wcsicmp(left.c_str(), right.c_str()) < 0;
    });
    std::map<std::string, std::size_t> targetSlots;
    for (const auto& folderTitle : folders) {
        std::string folderName = "?";
        try {
            folderName = utf8(folderTitle);
            const auto folder = modsRoot + L'\\' + folderTitle;
            const auto manifest = folder + L"\\mod.ini", user = folder + L"\\user.ini";
            const auto userEnabled = GetPrivateProfileIntW(L"Mod", L"Enabled",
                GetPrivateProfileIntW(L"Mod", L"Enabled", 1, manifest.c_str()), user.c_str());
            if (!userEnabled) {
                messages.push_back("Mod " + folderName + ": disabled.");
                continue;
            }
            if (index.mods.size() >= maxMods) throw std::runtime_error("more than 256 mods");
            wchar_t name[256]{};
            GetPrivateProfileStringW(L"Mod", L"Name", folderTitle.c_str(), name, 256, manifest.c_str());
            const Handle folderHandle = open(folder, true);
            if (!folderHandle.valid()) throw std::runtime_error("cannot open the mod folder");
            const auto resolvedFolder = finalPath(folderHandle.get());
            // [Options] in mod.ini are defaults; the same keys in user.ini survive mod updates.
            std::string options = "{";
            for (const auto& [key, fallback] : section(L"Options", manifest)) {
                wchar_t value[512]{};
                GetPrivateProfileStringW(L"Options", key.c_str(), fallback.c_str(), value, 512, user.c_str());
                options += "[" + luaQuote(utf8(key)) + "]=" +
                    (integer(value) ? utf8(value) : luaQuote(utf8(value))) + ",";
            }
            options += "}";
            const auto modIndex = static_cast<std::uint16_t>(index.mods.size());
            std::vector<Pending> pending;
            for (const bool override : {true, false}) {
                for (const auto& [target, file] : section(override ? L"Override" : L"Patch", manifest)) {
                    Pending item{};
                    item.target = targetKey(target, item.content);
                    item.override = override;
                    item.script.mod = modIndex;
                    std::wstring chunk = file;
                    std::replace(chunk.begin(), chunk.end(), L'\\', L'/');
                    item.script.chunk = "@mods/" + folderName + "/" + utf8(chunk);
                    auto source = readScript(folder, resolvedFolder, file);
                    if (override) {
                        item.script.source = std::move(source);
                    } else {
                        item.script.body = std::move(source);
                        item.script.source = "local original, patch, log, lib = ...\nlocal label = " +
                            luaQuote(folderName + "/" + utf8(chunk)) + "\nlocal options = " + options +
                            std::string(builderTail);
                    }
                    pending.push_back(std::move(item));
                }
            }
            std::vector<std::string> engine;
            for (const auto& [feature, value] : section(L"Engine", manifest)) {
                if (!integer(value)) throw std::runtime_error("[Engine] values must be 0 or 1: " + utf8(feature));
                if (std::wcstol(value.c_str(), nullptr, 10)) engine.push_back(utf8(feature));
            }
            if (index.scripts.size() + pending.size() > maxScripts) throw std::runtime_error("more than 4096 scripts");
            // Commit only after the whole mod validated. A mod without scripts may still bring assets.
            index.mods.push_back({folderName, utf8(name), folder, pending.size(), std::move(engine)});
            for (auto& item : pending) {
                auto& list = item.content ? index.contents : index.targets;
                // Names and markers live in separate namespaces; '\x01' never occurs in either.
                auto [slot, created] = targetSlots.try_emplace(
                    item.content ? '\x01' + item.target : item.target, list.size());
                if (created) list.push_back({item.target, item.content});
                auto& target = list[slot->second];
                const auto scriptIndex = static_cast<std::uint16_t>(index.scripts.size());
                if (item.override && target.override >= 0) {
                    messages.push_back("Mod " + folderName + ": override of " + item.target + " ignored; " +
                        index.mods[index.scripts[target.override].mod].folder + " already replaces it.");
                    continue;
                }
                index.scripts.push_back(std::move(item.script));
                if (item.override) target.override = scriptIndex;
                else target.patches.push_back(scriptIndex);
            }
            messages.push_back("Mod " + folderName + " (" + index.mods.back().name + "): " +
                std::to_string(pending.size()) + " script(s).");
        } catch (const std::exception& failure) {
            messages.push_back("Mod " + folderName + " skipped: " + failure.what());
        }
    }
    for (auto& target : index.targets) {
        const auto slash = target.path.rfind('/');
        target.basename = slash == std::string::npos ? 0 : slash + 1;
    }
    std::sort(index.targets.begin(), index.targets.end(), [](const Target& left, const Target& right) {
        return left.name() != right.name() ? left.name() < right.name() : left.path.size() > right.path.size();
    });
    return index;
}
}
