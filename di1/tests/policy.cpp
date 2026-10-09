// SPDX-License-Identifier: GPL-3.0-only
#include "di1/policy.hpp"
#include <cstdio>
#include <cstdlib>
#include <climits>
#include <cmath>

using namespace crabe::di1;
static int checks{};
static void check(bool ok, const char* message) {
    ++checks;
    if (!ok) { std::fprintf(stderr, "FAIL: %s\n", message); std::exit(1); }
}
static bool near(float a, float b) { return std::abs(a - b) < 0.1f; }
int main() {
    check(clampFrameLimit(-1) == 0 && clampFrameLimit(0) == 0,"uncapped option remains available");
    check(clampFrameLimit(120) == 120 && clampFrameLimit(240) == 240,"high frame limits are accepted");
    check(clampFrameLimit(1) == 15 && clampFrameLimit(2000) == 1000,"frame limit range matches upstream");
    for (const auto mode : {std::pair{3840u,2160u}, {3440u,1440u}, {1280u,800u}, {7680u,2160u},
                           {1920u,1080u}, {11520u,2160u}, {7680u,480u}, {32767u,480u}}) {
        check(plausibleMode(mode.first,mode.second), "modern display mode accepted");
        check(plausibleMode(mode.second,mode.first), "resolution ABI argument order does not matter");
    }
    for (const auto mode : {std::pair{0u,0u}, {3840u,0u}, {479u,1080u}, {32768u,2160u}, {UINT_MAX,UINT_MAX}})
        check(!plausibleMode(mode.first,mode.second), "invalid/overflow dimensions stay under original validation");
    const auto normal = centreUi({0,0,1,1},3840,2160);
    check(normal.x == 0 && normal.y == 0 && normal.width == 1 && normal.height == 1, "16:9 UI unchanged");
    const auto wide = centreUi({0,0,1,1},3440,1440);
    check(near(wide.x,440) && wide.y == 0 && near(wide.width * 3440,2560) && wide.height == 1, "ultrawide UI uses a centred 2560px frame");
    const auto tall = centreUi({0,0,1,1},1280,800);
    check(tall.x == 0 && near(tall.y,40) && tall.width == 1 && near(tall.height * 800,720), "16:10 UI centres vertically");
    const auto split = centreUi({0,0,0.5f,1},3840,2160);
    check(split.width == 0.5f && split.height == 1 && split.x == 0, "split-screen rectangle unchanged");
    const auto placed = centreUi({100,20,1,1},3440,1440);
    check(placed.x == 100 && placed.y == 20 && placed.width == 1, "explicitly placed movie unchanged");
    check(centreUi({0,0,1,1},0,0).width == 1, "uninitialized render target ignored");
    const auto stretched = centreUi({0,0,1,1},1280,720,32.0f/9.0f);
    check(near(stretched.x,320) && near(stretched.width,0.5f),"UI uses physical aspect when a 16:9 buffer fills a 32:9 window");
    constexpr double radians = 3.14159265358979323846 / 180.0;
    constexpr float baseline = 16.0f/9.0f;
    for (const float source : {30.0f,75.0f,90.0f,110.0f}) {
        const double sourceTangent = std::tan(source*radians*0.5);
        const double baseTangent = sourceTangent;
        for (const float renderAspect : {0.5625f,1.0f,4.0f/3.0f,baseline,21.0f/9.0f,32.0f/9.0f,48.0f/9.0f}) {
            const bool converted = renderAspect < baseline;
            const float native = static_cast<float>(2*std::atan(sourceTangent * (converted ? renderAspect/baseline : 1))/radians);
            for (const float physicalAspect : {0.5625f,1.0f,4.0f/3.0f,baseline,21.0f/9.0f,32.0f/9.0f,48.0f/9.0f}) {
                const float corrected = correctVerticalFov(native,renderAspect,baseline,converted,physicalAspect);
                const double tangent = std::tan(corrected*radians*0.5);
                const double expected = baseTangent * (std::max)(1.0f,baseline/physicalAspect);
                check(std::abs(tangent-expected)<0.00001,"FOV preserves 16:9 zoom and gains horizontal/vertical view for each shape");
                const float nodeAspect = correctProjectionAspect(1,renderAspect,physicalAspect);
                const double pixelShape = physicalAspect / (nodeAspect*renderAspect);
                check(std::abs(pixelShape-1)<0.00001,"render projection preserves circles when window and render aspect differ");
            }
        }
    }
    check(correctVerticalFov(0,3,1,true)==0,"orthographic projection is preserved");
    check(correctVerticalFov(75,0,1,true)==75,"invalid render dimensions leave FOV native");
    check(std::isnan(correctVerticalFov(NAN,2,1,true)),"non-finite FOV remains native");
    check(near(correctProjectionAspect(2,baseline,baseline),2),"custom native projection aspect is preserved when no compensation is needed");
    check(scaleCoordinate(640,3840,1280) == 1920, "window centre maps to render centre");
    check(scaleCoordinate(1920,1280,3840) == 640, "cursor centring uses inverse scale");
    check(scaleCoordinate(-80,3840,1280) == -240, "off-window mouse coordinates preserve sign");
    check(scaleCoordinate(640,3840,0) == 640, "minimized client does not divide by zero");
    check(scaleCoordinate(LONG_MAX,7680,1) == LONG_MAX, "coordinate multiplication saturates");
    std::printf("PASS: %d policy checks\n",checks);
}
