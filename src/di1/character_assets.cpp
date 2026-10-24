// SPDX-License-Identifier: GPL-3.0-only
#include "di1/character_assets.hpp"
#include "di1/character_gameplay.hpp"
#include <windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <map>
#include <stdexcept>

namespace crabe::di1 {
namespace {
std::string lower(std::string_view value) {
    std::string result(value);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + 32);
        if (c == '\\') c = '/';
    }
    return result;
}
bool safePath(std::string_view path) {
    if (path.empty() || path.front() == '/' || path.back() == '/' || path.find("..") != path.npos ||
        path.find("//") != path.npos) return false;
    return std::all_of(path.begin(), path.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '/' || c == '_' || c == '-' || c == '.';
    });
}
std::string identity(std::string_view namespaceKey, std::string_view path) {
    return "LiteralCrabAsset:" + std::string(namespaceKey) + ":" + std::string(path);
}
std::uint32_t word(std::string_view data, std::size_t at) {
    if (at > data.size() || data.size() - at < 4) throw std::runtime_error("truncated private texture bundle");
    std::uint32_t value{}; std::memcpy(&value, data.data() + at, 4); return value;
}
std::string textureHash(std::string_view namespaceKey, std::string_view path, std::string_view data) {
    std::uint64_t hash = 14695981039346656037ull;
    const auto update = [&](std::string_view value) { for (unsigned char c : value) { hash ^= c; hash *= 1099511628211ull; } };
    update("LiteralCrab private texture:"); update(namespaceKey); update(path); update(data);
    std::string bytes(8, '\0');
    for (unsigned i = 0; i < 8; ++i) bytes[i] = static_cast<char>(hash >> (i * 8));
    return bytes;
}
std::string hex(std::string_view data) {
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (unsigned char c : data) { result += digits[c >> 4]; result += digits[c & 15]; }
    return result;
}
std::string unhex(std::string_view text) {
    if (text.size() != 16) throw std::runtime_error("private texture filename must contain its 16-digit hash");
    const auto nibble = [](char c) -> unsigned {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("invalid private texture hash");
    };
    std::string result;
    for (unsigned i = 0; i < 16; i += 2) result += static_cast<char>((nibble(text[i]) << 4) | nibble(text[i + 1]));
    return result;
}
// MATP v2 stores shader names, material names, shader indices and cache keys in
// separate arrays. A malformed shader index reaches an unchecked native pointer
// dereference. Validate metadata before publishing private resources; parameter
// blocks remain opaque because their sizes are supplied by the native shader.
void validateMaterials(std::string_view data) {
    const auto invalid=[] { throw std::runtime_error("invalid private MATP v2 metadata"); };
    if (data.size()<20 || word(data,4)!=2) invalid();
    const auto materials=word(data,12), shaders=word(data,16);
    if (materials>65535 || shaders>65535 || materials>data.size()/2 || shaders>data.size()/2) invalid();
    std::size_t at=20;
    const auto strings=[&](unsigned count) {
        for (unsigned i=0;i<count;++i) {
            const auto end=data.find('\0',at);
            if (end==data.npos || end==at || end-at>255) invalid();
            for (auto p=at;p<end;++p) if (static_cast<unsigned char>(data[p])<32 ||
                static_cast<unsigned char>(data[p])>126) invalid();
            at=end+1;
        }
    };
    strings(shaders);
    at=(at+3)&~std::size_t(3);
    if (at>data.size() || data.size()-at<4 || word(data,at)!=materials) invalid();
    at+=4;strings(materials);at=(at+1)&~std::size_t(1);
    if (at>data.size() || materials*2ull>data.size()-at) invalid();
    for (unsigned i=0;i<materials;++i) {
        const auto p=at+i*2;
        const unsigned shader=static_cast<unsigned char>(data[p]) |
            (static_cast<unsigned char>(data[p+1])<<8);
        if (shader>=shaders) invalid();
    }
    at=(at+materials*2ull+3)&~std::size_t(3);
    if (at>data.size() || materials*4ull>data.size()-at) invalid();
    at=(at+materials*4ull+15)&~std::size_t(15);
    if (at>data.size() || (materials && at==data.size())) invalid();
}
void rebindTextures(std::string& data, const std::map<std::string, std::string>& textures) {
    if (data.size() < 16 || data.substr(0, 4) != "BNDL" || word(data, 4) != 1 || word(data, 8) != data.size())
        throw std::runtime_error("private texture rebinding requires a BNDL v1 / TEXB v3 bundle");
    const auto chunks = word(data, 12);
    if (!chunks || chunks > (data.size()-16)/12) throw std::runtime_error("invalid private TEXB chunk count");
    std::size_t chunk=16;
    bool foundTable{};
    for (unsigned c=0;c<chunks;++c) {
        const auto size=word(data,chunk+8);
        if (size<12 || size>data.size()-chunk) throw std::runtime_error("invalid private TEXB chunk bounds");
        if (data.compare(chunk,4,"TEXB")==0) {
            if (size<20 || word(data,chunk+4)!=3) throw std::runtime_error("private texture rebinding requires TEXB v3");
            foundTable=true;
            const auto count=word(data,chunk+12), names=word(data,chunk+16);
            const auto entries=(chunk+20ull+names+3)&~3ull;
            const auto end=chunk+size;
            if (entries>end || count*12ull>end-entries) throw std::runtime_error("invalid private texture entries");
            for (unsigned i=0;i<count;++i) {
                const auto at=static_cast<std::size_t>(entries+i*12ull);
                // Model material bundles have a nameless TEXB table. Icon
                // bundles carry name offsets in the same 12-byte entries.
                if (names) {
                    const auto name=word(data,at+8);
                    if (name>=names || data.find('\0',chunk+20+name)>=chunk+20ull+names)
                        throw std::runtime_error("invalid private texture name");
                }
                if (const auto found=textures.find(data.substr(at,8));found!=textures.end()) data.replace(at,8,found->second);
            }
        } else if (data.compare(chunk,4,"MATP")==0) {
            validateMaterials(std::string_view(data).substr(chunk,size));
        }
        chunk+=size;
    }
    if (chunk!=data.size() || !foundTable) throw std::runtime_error("invalid private TEXB bundle extent");
}
void alias(ResourceAliases& aliases, std::string from, const std::string& to) {
    for (const auto& [key, value] : aliases) if (key == from) {
        if (value != to) throw std::runtime_error("ambiguous private resource alias: " + from);
        return;
    }
    aliases.emplace_back(std::move(from), to);
}
// Change resource path literals only. Native instructions, indices and existing
// string offsets keep their original locations, as in the private weapon clone.
std::string remapScript(std::string data, const ResourceAliases& aliases) {
    if (data.size() < 0x68 || data.substr(0, 12) != std::string_view("SLNG\x0c\x10\0\0\x78\x56\x34\x12", 12) ||
        word(data, 12) != data.size()) throw std::runtime_error("unsupported private SLNG script");
    const auto count = word(data, 0x30);
    const auto list = 0x65ull + word(data, 0x64);
    if (!word(data, 0x64) || count > 65535 || list > data.size() || count * 4ull > data.size() - list)
        throw std::runtime_error("invalid private script literal table");
    std::vector<std::pair<std::string, std::string>> replacements;
    for (unsigned i = 0; i < count; ++i) {
        const auto at = static_cast<std::size_t>(list + i * 4ull);
        const auto offset = word(data, at); if (!offset) continue;
        const auto start = at + 1ull + offset;
        if (start >= data.size()) throw std::runtime_error("invalid private script literal");
        const auto end = data.find('\0', static_cast<std::size_t>(start));
        if (end == data.npos) throw std::runtime_error("unterminated private script literal");
        const auto value = data.substr(static_cast<std::size_t>(start), end - static_cast<std::size_t>(start));
        if (value.find('/') == value.npos && value.find('\\') == value.npos) continue;
        const auto mapped = privateAssetPath(aliases, value);
        if (mapped != value) replacements.emplace_back(value, mapped);
    }
    std::sort(replacements.begin(), replacements.end());
    replacements.erase(std::unique(replacements.begin(), replacements.end()), replacements.end());
    for (const auto& [from, to] : replacements) data = remapSlngLiteral(data, from, to);
    return data;
}
}

void rebindBundleTextures(std::string& bundle, const std::map<std::string, std::string>& textures) {
    rebindTextures(bundle, textures);
}

std::string privateAssetPath(const ResourceAliases& aliases, std::string_view logical) {
    const auto split = logical.find('|');
    const auto prefix = split == logical.npos ? std::string_view{} : logical.substr(0, split + 1);
    const auto path = logical.substr(prefix.size());
    const auto key = lower(path);
    for (const auto& [from, to] : aliases) if (from == key) return std::string(prefix) + to;
    return std::string(logical);
}

std::string customAssetPath(const CustomFigure& f, std::string_view logical) {
    return privateAssetPath(f.assetAliases, logical);
}

void preparePrivateAssetDirectory(mods::AssetIndex& assets, const std::wstring& modPath,
    std::string_view directory, std::string_view namespaceKey, ResourceAliases& aliases) {
    namespace fs = std::filesystem;
    if (!safePath(namespaceKey) || lower(namespaceKey) != namespaceKey || namespaceKey == "." ||
        namespaceKey.starts_with("./") || namespaceKey.ends_with("/.") || namespaceKey.find("/./") != namespaceKey.npos)
        throw std::runtime_error("invalid private resource namespace");
    if (modPath.empty() || !safePath(lower(directory))) throw std::runtime_error("invalid private asset folder");
    auto root = fs::path(modPath);
    // Reject junctions/symlinks in any directory component, including
    // intermediate ones above the actual mounted folder.
    for (const auto& component : fs::path(directory)) {
        root /= component;
        const auto flags = GetFileAttributesW(root.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("private asset folder missing or traverses a reparse point");
    }
    const auto prefix = "characters/literalcrab/assets/" + std::string(namespaceKey) + "/";
    std::map<std::string, std::string> files;
    std::size_t bytes{};
    for (const auto& file : fs::recursive_directory_iterator(root)) {
        const auto flags = GetFileAttributesW(file.path().c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("private assets cannot contain reparse points");
        if (file.is_directory()) continue;
        const auto relative = lower(file.path().lexically_relative(root).generic_string());
        if (!safePath(relative) || prefix.size() + relative.size() >= mods::maxChunkName - 32)
            throw std::runtime_error("invalid or too long private resource path");
        mods::AssetMember source; source.file = file.path().wstring();
        std::string data;
        if (!assets.read(source, data)) throw std::runtime_error("cannot read private resource: " + relative);
        bytes += data.size();
        if (files.size() >= 4096 || bytes > (512ull << 20)) throw std::runtime_error("private asset folder exceeds 4096 files or 512 MiB");
        if (!files.emplace(relative, std::move(data)).second) throw std::runtime_error("duplicate private resource path: " + relative);
    }
    if (files.empty()) throw std::runtime_error("private asset folder is empty");
    ResourceAliases prepared;
    std::map<std::string, std::string> textures;
    for (const auto& [path, data] : files) {
        auto target = prefix + path;
        if (path.ends_with(".tbody")) {
            const auto leaf = path.substr(path.rfind('/') + 1);
            const auto sourceHash = unhex(leaf.substr(0, leaf.size() - 6));
            const auto privateHash = textureHash(namespaceKey, path, data);
            const auto [found, added] = textures.emplace(sourceHash, privateHash);
            if (!added && found->second != privateHash) throw std::runtime_error("ambiguous private texture hash");
            target = "textures/" + hex(privateHash) + ".tbody";
        }
        alias(prepared, path, target);
        const auto dot = path.rfind('.'), slash = path.rfind('/');
        if (!path.ends_with(".tbody") && dot != path.npos && (slash == path.npos || dot > slash))
            alias(prepared, path.substr(0, dot), target.substr(0, target.rfind('.')));
    }
    std::vector<mods::AssetMember> pending;
    std::vector<mods::AssetRedirect> disk;
    for (auto& [path, data] : files) {
        const auto target = privateAssetPath(prepared, path);
        if (assets.member(target) || assets.find(target)) throw std::runtime_error("private resource namespace already claimed: " + target);
        const auto original = data;
        if (path.ends_with(".dnax")) data = identifyCustomDna(data, identity(namespaceKey, path));
        if (path.ends_with(".sx")) data = remapScript(std::move(data), prepared);
        if (path.ends_with(".mtb")) rebindTextures(data, textures);
        // Opaque resources (including native model sidecars and audio
        // banks) can also be opened by Windows instead of the member
        // loader. Redirect only unchanged bytes to their own mod file.
        if (data == original) disk.push_back({target, (root / fs::path(path)).wstring()});
        mods::AssetMember member; member.name = target;
        member.generated = std::make_shared<const std::string>(std::move(data));
        pending.push_back(std::move(member));
    }
    // All input validation and transformations succeed before publish.
    assets.members.reserve(assets.members.size() + pending.size());
    assets.redirects.reserve(assets.redirects.size() + disk.size());
    for (auto& member : pending) assets.members.push_back(std::move(member));
    for (auto& redirect : disk) assets.redirects.push_back(std::move(redirect));
    std::sort(assets.members.begin(), assets.members.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
    std::sort(assets.redirects.begin(), assets.redirects.end(), [](const auto& a, const auto& b) { return a.path < b.path; });
    aliases = std::move(prepared);
}

void prepareCustomAssetDirectories(mods::AssetIndex& assets, std::vector<CustomFigure>& figures,
    std::vector<std::string>& messages) {
    std::erase_if(figures, [&](CustomFigure& f) {
        if (f.assetDirectory.empty()) return false;
        try {
            const auto before = assets.members.size();
            preparePrivateAssetDirectory(assets, f.modPath, f.assetDirectory, std::to_string(f.sku), f.assetAliases);
            messages.push_back("Custom assets " + f.name + ": " + std::to_string(assets.members.size() - before) + " file(s) mounted privately from its mod folder");
            return false;
        } catch (const std::exception& error) {
            messages.push_back("Custom assets " + f.name + ": REJECTED: " + error.what()); return true;
        }
    });
}

std::string privateAssetDnaPatch(const ResourceAliases& aliases) {
    if (aliases.empty()) return {};
    std::string code = "do local aliases={\n";
    for (const auto& [from, to] : aliases) code += "[" + mods::luaQuote(from) + "]=" + mods::luaQuote(to) + ",\n";
    return code + R"lua(}
local seen={}
local function visit(t)
    if seen[t] then return end
    seen[t]=true
    for key,value in pairs(t) do
        if type(value)=='table' then visit(value)
        elseif type(value)=='string' then
            local prefix,path=string.match(value,'^([^|]+|)(.*)$')
            if not prefix then prefix,path='',value end
            -- Bare strings also contain actor masks, identity and state names.
            -- Only directory-qualified references participate in automatic DNA remapping.
            local normalized=string.lower((string.gsub(path,'\\','/')))
            local mapped=string.find(normalized,'/',1,true) and aliases[normalized]
            if mapped then t[key]=prefix..mapped end
        end
    end
end
if type(DNA)=='table' then visit(DNA) end
end
)lua";
}

std::string customAssetDnaPatch(const CustomFigure& f) {
    return privateAssetDnaPatch(f.assetAliases);
}

void addCustomAssetPatches(mods::Index& index, const std::vector<CustomFigure>& figures) {
    for (const auto& f : figures) if (!f.assetAliases.empty()) {
        const auto body = customAssetDnaPatch(f);
        for (const auto& [from, to] : f.assetAliases) if (from.ends_with(".dnax")) {
            const auto marker = identity(std::to_string(f.sku), from), content = "contains:" + marker;
            index.addBuiltinPatch({to, marker, content}, "custom_asset_" + std::to_string(f.sku) + "_" + from, body, "{}");
        }
    }
}
}
