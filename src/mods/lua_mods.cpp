// SPDX-License-Identifier: GPL-3.0-only
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <iterator>
#include <string>
#include <string_view>

namespace crabe::mods {
namespace {
    LuaApi api{};
    Index index;
    LogFunction log = [](const char*, ...) {};
    bool trace{};
    // Cleared after a native fault; chunks then load exactly as the game sends them.
    std::atomic<bool> active{};
    std::string prelude;
    std::vector<LuaCFunction> functions;

    // Library functions patches may use. File, process, module and garbage-collector access,
    // and the game's shared random generator, are deliberately absent.
    constexpr std::string_view allowed[] = {
        "assert", "error", "getfenv", "getmetatable", "next", "pcall", "rawequal", "rawget", "rawset",
        "select", "setfenv", "setmetatable", "tonumber", "tostring", "type", "unpack", "xpcall",
        "table.concat", "table.insert", "table.maxn", "table.remove", "table.sort",
        "string.byte", "string.char", "string.find", "string.format", "string.gmatch", "string.gsub",
        "string.len", "string.lower", "string.match", "string.rep", "string.reverse", "string.sub",
        "string.upper",
        "math.abs", "math.acos", "math.asin", "math.atan", "math.atan2", "math.ceil", "math.cos",
        "math.cosh", "math.deg", "math.exp", "math.floor", "math.fmod", "math.frexp", "math.ldexp",
        "math.log", "math.log10", "math.max", "math.min", "math.modf", "math.pow", "math.rad",
        "math.sin", "math.sinh", "math.sqrt", "math.tan", "math.tanh",
    };
    // Used by the patch wrapper and the lib prelude themselves.
    constexpr std::string_view needed[] = {"getfenv", "setfenv", "setmetatable", "pcall", "tostring", "next", "rawget"};

    int printable(std::size_t length) { return static_cast<int>((std::min)(length, std::size_t(1600))); }

    // Lua: log(message). Bound into every patch wrapper, which prefixes the mod and file.
    int __cdecl luaLog(lua_State* state) {
        std::size_t length{};
        if (const char* message = api.toLString(state, 1, &length))
            log("Mod %.*s", printable(length), message);
        return 0;
    }

    // Unnamed chunks are shown by their first line, which a "contains:" marker can use.
    void traceChunk(std::string_view identity, const char* data, std::size_t size) {
        char name[maxChunkName];
        if (normalize(identity, name)) {
            log("Lua chunk %.*s (%zu bytes)", printable(identity.size()), identity.data(), size);
            return;
        }
        if (size >= 4 && !std::memcmp(data, "\x1bLua", 4)) {
            log("Lua chunk <unnamed bytecode> (%zu bytes)", size);
            return;
        }
        char preview[81]{};
        std::size_t length = 0;
        for (std::size_t offset = 0; offset < size && length < 80; ++offset) {
            const auto ch = static_cast<unsigned char>(data[offset]);
            if ((ch == '\n' || ch == '\r') && length) break;
            if (ch >= 32 && ch < 127) preview[length++] = static_cast<char>(ch);
        }
        log("Lua chunk <unnamed> (%zu bytes): %s", size, preview);
    }

    void logError(lua_State* state, const char* what, const char* chunk) {
        std::size_t length{};
        const char* message = api.toLString(state, -1, &length);
        log("Mod %s %s: %.*s", chunk + 1, what, message ? printable(length) : 7, message ? message : "unknown");
    }

    // Stack on entry: [... chunk] with the chunk at index top. On success the chunk slot holds
    // the wrapper; on any failure the stack is restored and the compiled game chunk is left
    // untouched. Locals stay trivially destructible because Lua reports out-of-memory with
    // longjmp. Library functions are handed over one per call, so stack use stays constant.
    bool compose(lua_State* state, const Script& patch, int top) {
        const char* failure = "library setup failed";
        if (!api.loadBuffer(state, prelude.data(), prelude.size(), "=LiteralCrab library") &&
            !api.pcall(state, 0, 2, 0)) {
            bool added = true;
            for (const auto function : functions) {
                api.pushValue(state, top + 1);
                api.pushClosure(state, function, 0);
                if (api.pcall(state, 1, 0, 0)) { added = false; break; }
            }
            if (added && !api.pcall(state, 0, 1, 0)) {
                failure = "builder failed";
                if (!api.loadBuffer(state, patch.source.data(), patch.source.size(), "=LiteralCrab patch builder")) {
                    api.pushValue(state, top);
                    failure = "failed to compile";
                    if (!api.loadBuffer(state, patch.body.data(), patch.body.size(), patch.chunk.c_str())) {
                        api.pushClosure(state, &luaLog, 0);
                        api.pushValue(state, top + 2);
                        failure = "was not applied";
                        if (!api.pcall(state, 4, 1, 0)) {
                            api.insert(state, top);
                            api.setTop(state, top);
                            return true;
                        }
                    }
                }
            }
        }
        logError(state, failure, patch.chunk.c_str());
        api.setTop(state, top);
        return false;
    }

    bool guardedCompose(lua_State* state, const Script& patch) {
        int top = -1;
        __try {
            top = api.getTop(state);
            return compose(state, patch, top);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            active.store(false, std::memory_order_relaxed);
            log("Mod loader disabled after native exception 0x%08lX in %s.", GetExceptionCode(), patch.chunk.c_str() + 1);
        }
        __try {
            if (top >= 0) api.setTop(state, top);
        } __except (EXCEPTION_EXECUTE_HANDLER) {}
        return false;
    }
}

bool configure(const LuaApi& resolved, Index loaded, LogFunction logger, bool traceChunks) {
    log = logger;
    // Generated once: collects the allowlisted functions in order, then builds lib from them.
    prelude = "local functions, count = {}, 0\n"
        "return function(f) count = count + 1; functions[count] = f end, function()\n"
        "local lib = {table = {}, string = {}, math = {}}\n";
    functions.clear();
    std::vector<std::string_view> names;
    for (const auto& entry : resolved.library) {
        if (!entry.function || std::find(std::begin(allowed), std::end(allowed), entry.name) == std::end(allowed) ||
            std::find(names.begin(), names.end(), entry.name) != names.end()) continue;
        names.push_back(entry.name);
        functions.push_back(entry.function);
        prelude += "lib." + entry.name + " = functions[" + std::to_string(functions.size()) + "]\n";
    }
    for (const auto name : needed)
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            log("Mods: the engine's Lua library lacks %.*s, which the patch wrapper needs.",
                static_cast<int>(name.size()), name.data());
            return false;
        }
    prelude += "local next, rawget = lib.next, lib.rawget\n"
        "lib.pairs = function(t) return next, t, nil end\n"
        "local function iterate(t, i) i = i + 1; local v = rawget(t, i); if v ~= nil then return i, v end end\n"
        "lib.ipairs = function(t) return iterate, t, 0 end\n"
        "lib.math.pi = 3.141592653589793\n"
        "lib.math.huge = 1 / 0\n"
        "return lib\nend\n";
    api = resolved;
    index = std::move(loaded);
    trace = traceChunks;
    active.store(true, std::memory_order_release);
    return true;
}

int __cdecl loadBuffer(lua_State* state, const char* data, std::size_t size, const char* name) {
    if (!active.load(std::memory_order_acquire) || !data)
        return api.loadBuffer(state, data, size, name);
    const auto identity = chunkIdentity(data, size, name);
    if (trace) traceChunk(identity, data, size);
    const Target* matches[maxMatches];
    const auto count = index.find(identity, std::string_view(data, size), matches);
    if (!count) return api.loadBuffer(state, data, size, name);
    // The most specific path that replaces the chunk wins; patches from every match apply.
    const Target* target = nullptr;
    for (std::size_t match = 0; match < count && !target; ++match)
        if (matches[match]->override >= 0) target = matches[match];
    int status = 1;
    if (target) {
        const auto& replacement = index.scripts[target->override];
        status = api.loadBuffer(state, replacement.source.data(), replacement.source.size(), replacement.chunk.c_str());
        if (status) {
            logError(state, "failed to compile; the game chunk is used instead", replacement.chunk.c_str());
            api.setTop(state, -2);
        } else if (trace) {
            log("Mod %s replaced %s.", replacement.chunk.c_str() + 1, target->path.c_str());
        }
    }
    if (status) status = api.loadBuffer(state, data, size, name);
    if (status) return status;
    for (std::size_t match = 0; match < count; ++match)
        for (const auto patch : matches[match]->patches) {
            if (!active.load(std::memory_order_relaxed)) return status;
            if (guardedCompose(state, index.scripts[patch]) && trace)
                log("Mod %s patched %s.", index.scripts[patch].chunk.c_str() + 1, matches[match]->path.c_str());
        }
    return status;
}
}
