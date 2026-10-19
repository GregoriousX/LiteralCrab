// SPDX-License-Identifier: GPL-3.0-only
#include "../../src/di1/controller.cpp"
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
namespace {
std::vector<std::string> messages;
DWORD result = ERROR_SUCCESS, seenUser{};
unsigned nativeCalls{}, imports{};
DWORD WINAPI nativeGetState(DWORD user, crabe::di1::State* state) {
    seenUser = user; ++nativeCalls;
    if (result == ERROR_SUCCESS && state) { state->packet = 42; state->gamepad.buttons = 0x4101; }
    SetLastError(0x1234ABCD); return result;
}
unsigned failures{};
#define CHECK(value) do { if (!(value)) { std::printf("FAIL line %d: %s\n", __LINE__, #value); ++failures; } } while (0)
}
namespace crabe::di1 {
void log(const char* format, ...) {
    char line[1024]; va_list args; va_start(args, format);
    std::vsnprintf(line, sizeof(line), format, args); va_end(args);
    messages.emplace_back(line); SetLastError(0xFFEEDDCC);
}
bool ImportPatch::install(HMODULE, const char* dll, const char* name, void* detour) {
    CHECK(std::strcmp(dll, "XINPUT9_1_0.dll") == 0 && std::strcmp(name, "XInputGetState") == 0);
    CHECK(detour == reinterpret_cast<void*>(&getState)); ++imports;
    original = reinterpret_cast<void*>(&nativeGetState); return true;
}
}
int main() {
    using namespace crabe::di1;
    Config options;
    CHECK(installControllerHooks(nullptr, options) && imports == 0);
    options.traceCharacterFreedom = true;
    CHECK(installControllerHooks(nullptr, options) && imports == 1);
    messages.clear();
    State state{};
    CHECK(getState(0, &state) == ERROR_SUCCESS && state.gamepad.buttons == 0x4101 && state.packet == 42);
    CHECK(GetLastError() == 0x1234ABCD && messages.size() == 1);
    CHECK(getState(0, &state) == ERROR_SUCCESS && GetLastError() == 0x1234ABCD && messages.size() == 1);
    std::memset(&state, 0xCD, sizeof(state)); const auto untouched = state;
    result = ERROR_DEVICE_NOT_CONNECTED;
    CHECK(getState(0, &state) == result && GetLastError() == 0x1234ABCD);
    CHECK(!std::memcmp(&state, &untouched, sizeof(state)));
    CHECK(messages.size() == 2 && messages.back().find("connected=0") != std::string::npos);
    CHECK(getState(5, nullptr) == result && seenUser == 5 && GetLastError() == 0x1234ABCD);
    CHECK(messages.size() == 2);
    result = ERROR_SUCCESS;
    CHECK(getState(0, &state) == result && messages.size() == 3);
    options.traceCharacterFreedom = false; options.swapXY = true;
    CHECK(installControllerHooks(nullptr, options)); messages.clear();
    CHECK(getState(0, &state) == result && state.gamepad.buttons == 0x8101 && GetLastError() == 0x1234ABCD);
    CHECK(messages.empty());
    options.traceCharacterFreedom = true;
    CHECK(installControllerHooks(nullptr, options)); messages.clear();
    for (unsigned i = 0; i < 150; ++i) {
        result = i % 2 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
        CHECK(getState(1, &state) == result && GetLastError() == 0x1234ABCD);
    }
    CHECK(messages.size() == 129 && messages.back().find("limit reached") != std::string::npos);
    CHECK(nativeCalls == 156);
    std::printf("Controller native results/state/LastError/transition budget/swap: %s\n", failures ? "FAILED" : "passed");
    return failures ? 1 : 0;
}
