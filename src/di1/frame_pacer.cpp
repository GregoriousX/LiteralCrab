// SPDX-License-Identifier: GPL-3.0-only
// Ported from CrabeLoader dev RenderHook::paceFrame: QPC + waitable timer + short final spin.
#include "di1/frame_pacer.hpp"
#include <intrin.h>

namespace crabe::di1 {
namespace {
    long long now() { LARGE_INTEGER value{}; QueryPerformanceCounter(&value); return value.QuadPart; }
}
FramePacer::~FramePacer() { if (timer) CloseHandle(timer); }
void FramePacer::reset() noexcept { next = 0; windowStart = 0; windowFrames = 0; lastFps = 0; }
void FramePacer::pace(unsigned limit) {
    if (!frequency) { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); frequency = value.QuadPart; }
    if (!frequency || !limit) { next = 0; return; }
    const long long period = frequency / limit;
    long long current = now();
    // Loads and Alt+Tab do not cause a burst of catch-up frames.
    if (!next || current - next > period) next = current;
    if (!timer) {
        timer = CreateWaitableTimerExW(nullptr,nullptr,0x2,TIMER_ALL_ACCESS);
        if (!timer) timer = CreateWaitableTimerExW(nullptr,nullptr,0,TIMER_ALL_ACCESS);
    }
    const long long spin = frequency * 3 / 2000;
    for (;;) {
        current = now();
        const long long remaining = next - current;
        if (remaining <= 0) break;
        if (remaining > spin) {
            LARGE_INTEGER due{};
            due.QuadPart = -((remaining - spin) * 10000000LL / frequency);
            if (timer && SetWaitableTimer(timer,&due,0,nullptr,nullptr,FALSE)) WaitForSingleObject(timer,INFINITE);
            else Sleep(1);
        } else YieldProcessor();
    }
    next += period;
}
float FramePacer::recordPresentedFrame() {
    if (!frequency) { LARGE_INTEGER value{}; QueryPerformanceFrequency(&value); frequency = value.QuadPart; }
    if (!frequency) return 0;
    const long long current = now();
    if (!windowStart) { windowStart = current; windowFrames = 0; }
    ++windowFrames;
    if (current - windowStart >= frequency * 2) {
        lastFps = static_cast<float>(static_cast<double>(windowFrames) * frequency / (current - windowStart));
        windowFrames = 0; windowStart = current;
        return lastFps;
    }
    return 0;
}
}
