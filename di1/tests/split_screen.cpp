// SPDX-License-Identifier: GPL-3.0-only
#include "../../src/di1/split_screen.cpp"
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
namespace {
std::vector<std::string> messages;
unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)

// Fake engine objects with the vtable layouts the module calls through (thiscall = fastcall
// with an unused EDX).
struct Device { void** vtable; const char* name; bool connected, pressed, released; };
Device devices[3];
int owners[3]{};
unsigned players[0x40]{};
std::vector<unsigned> joins;
unsigned intercepts{};

unsigned __fastcall count(void*, void*) { return 3; }
void* __fastcall device(void*, void*, unsigned index) { return index < 3 ? &devices[index] : nullptr; }
int __fastcall playerOf(void*, void*, unsigned index) { return index < 3 ? owners[index] : -1; }
const char* __fastcall name(Device* self, void*) { return self->name; }
bool __fastcall connected(Device* self, void*) { return self->connected; }
bool __fastcall pressed(Device* self, void*, int button, bool) { return button == 0xD && self->pressed; }
bool __fastcall released(Device* self, void*, int button, bool) { return button == 0xD && self->released; }
void __fastcall addPlayer(void*, void*, unsigned index) { joins.push_back(index); players[0x9C / 4] = 2; owners[index] = 1; }
void __fastcall intercept(void*, void*) { ++intercepts; }

void* inputCalls[0xA0 / 4]{};
void* deviceCalls[0x20 / 4]{};
void* loopCalls[0xB8 / 4]{};
struct { void** vtable; } input{inputCalls}, loop{loopCalls};
}
namespace crabe::di1 {
void log(const char* format, ...) {
    char line[1024]; va_list args; va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args); va_end(args);
    messages.emplace_back(line);
}
bool readable(const void*, std::size_t) noexcept { return true; }
bool codeMatches(std::uintptr_t, const char*) noexcept { return true; }
}
int main() {
    using namespace crabe::di1;
    Config options;
    CHECK(installSplitScreen(nullptr, options) && messages.empty());

    inputCalls[0x60 / 4] = reinterpret_cast<void*>(&count);
    inputCalls[0x88 / 4] = reinterpret_cast<void*>(&device);
    inputCalls[0x9C / 4] = reinterpret_cast<void*>(&playerOf);
    deviceCalls[0x08 / 4] = reinterpret_cast<void*>(&name);
    deviceCalls[0x10 / 4] = reinterpret_cast<void*>(&connected);
    deviceCalls[0x18 / 4] = reinterpret_cast<void*>(&pressed);
    deviceCalls[0x1C / 4] = reinterpret_cast<void*>(&released);
    loopCalls[0xAC / 4] = reinterpret_cast<void*>(&addPlayer);
    devices[0] = {deviceCalls, "Keyboard", true, true, false};
    devices[1] = {deviceCalls, "XInput", true, false, false};
    devices[2] = {deviceCalls, "XInput", true, false, false};
    owners[0] = -1; owners[1] = 0; owners[2] = -1;
    players[0x9C / 4] = 1;

    // The globals live at fixed offsets from the image base: back them with one fake image.
    std::vector<unsigned char> image(playersGlobal + 4);
    base = reinterpret_cast<std::uintptr_t>(image.data());
    *reinterpret_cast<void**>(base + inputGlobal) = &input;
    *reinterpret_cast<void**>(base + playersGlobal) = players;
    originalIntercept = reinterpret_cast<InterceptFn>(&intercept);

    // The keyboard and player 1's pad never join; the game's handling still runs.
    devices[1].pressed = true;
    hookIntercept(&loop, nullptr);
    CHECK(joins.empty() && intercepts == 1);
    // START on the unowned pad joins it as a new player.
    devices[1].pressed = false; devices[2].pressed = true;
    hookIntercept(&loop, nullptr);
    CHECK(joins.size() == 1 && joins[0] == 2 && intercepts == 2);
    CHECK(messages.size() == 1 && messages[0].find("device 2") != std::string::npos &&
          messages[0].find("1 -> 2") != std::string::npos);
    // Its release is kept from the game's START handling (which would pause) once.
    devices[2].pressed = false; devices[2].released = true;
    hookIntercept(&loop, nullptr);
    CHECK(intercepts == 2);
    hookIntercept(&loop, nullptr);
    CHECK(intercepts == 3);
    // With two local players nobody else joins.
    owners[2] = -1; devices[2].released = false; devices[2].pressed = true;
    hookIntercept(&loop, nullptr);
    CHECK(joins.size() == 1 && intercepts == 4);
    std::printf("Split screen join/keyboard/owned pad/release swallow/full game: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
