// SPDX-License-Identifier: GPL-3.0-only
#include "mods/lua_mods.hpp"
#include <windows.h>
#include <algorithm>
#include <atomic>

namespace crabe::mods {
namespace {
    LuaApi api{};
    Index index;
    LogFunction log = [](const char*, ...) {};
    bool trace{};
    // Cleared after a native fault; chunks then load exactly as the game sends them.
    std::atomic<bool> active{};

    int printable(std::size_t length) { return static_cast<int>((std::min)(length, std::size_t(1600))); }

    // Lua: log(message). Bound into every patch wrapper, which prefixes the mod and file.
    int __cdecl luaLog(lua_State* state) {
        std::size_t length{};
        if (const char* message = api.toLString(state, 1, &length))
            log("Mod %.*s", printable(length), message);
        return 0;
    }

    void logError(lua_State* state, const char* what, const char* chunk) {
        std::size_t length{};
        const char* message = api.toLString(state, -1, &length);
        log("Mod %s %s: %.*s", chunk + 1, what, message ? printable(length) : 7, message ? message : "unknown");
    }

    // Stack on entry: [... chunk]. On success the chunk slot holds the wrapper; on any failure the
    // stack is restored and the compiled game chunk is left untouched. Locals stay trivially
    // destructible because Lua reports out-of-memory with longjmp.
    bool compose(lua_State* state, const Script& patch, int top) {
        const char* failure = "builder failed";
        if (!api.loadBuffer(state, patch.source.data(), patch.source.size(), "=LiteralCrab patch builder")) {
            api.pushValue(state, -2);
            failure = "failed to compile";
            if (!api.loadBuffer(state, patch.body.data(), patch.body.size(), patch.chunk.c_str())) {
                api.pushClosure(state, &luaLog, 0);
                failure = "was not applied";
                if (!api.pcall(state, 3, 1, 0)) {
                    api.insert(state, -2);
                    api.setTop(state, top);
                    return true;
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

void configure(const LuaApi& resolved, Index loaded, LogFunction logger, bool traceChunks) {
    api = resolved;
    index = std::move(loaded);
    log = logger;
    trace = traceChunks;
    active.store(true, std::memory_order_release);
}

int __cdecl loadBuffer(lua_State* state, const char* data, std::size_t size, const char* name) {
    if (!active.load(std::memory_order_acquire) || !data)
        return api.loadBuffer(state, data, size, name);
    const auto identity = chunkIdentity(data, size, name);
    if (trace) log("Lua chunk %.*s (%zu bytes)", printable(identity.size()), identity.data(), size);
    const Target* matches[maxMatches];
    const auto count = index.find(identity, matches);
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
