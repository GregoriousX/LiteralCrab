// SPDX-License-Identifier: GPL-3.0-only
#include "di1/frame_pacer.hpp"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using namespace crabe::di1;
static double seconds() {
    LARGE_INTEGER frequency{}, value{}; QueryPerformanceFrequency(&frequency); QueryPerformanceCounter(&value);
    return static_cast<double>(value.QuadPart)/frequency.QuadPart;
}
int main() {
    for (unsigned cap : {60u,120u,240u}) {
        FramePacer pacer;
        const unsigned frames = cap / 2 + 1;
        const double start = seconds();
        for (unsigned i = 0; i < frames; ++i) pacer.pace(cap);
        const double duration = seconds() - start;
        const double measured = (frames - 1) / duration;
        std::printf("Cap %u: measured %.2f FPS over %.3f seconds\n",cap,measured,duration);
        if (duration < 0.49 || measured > cap * 1.03) return 1;
    }
    FramePacer unlimited;
    const double start = seconds();
    for (unsigned i = 0; i < 10000; ++i) unlimited.pace(0);
    if (seconds() - start > 0.25) return 1;
    std::puts("PASS: frame pacing at 60/120/240 FPS and uncapped");
    return 0;
}
