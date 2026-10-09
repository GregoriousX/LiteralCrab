// SPDX-License-Identifier: GPL-3.0-only
// Resolution bounds and 16:9 UI policy ported from CrabeLoader's engine_resolution.cpp.
#pragma once
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <limits>

namespace crabe::di1 {
enum class Mode { Borderless, Windowed, Fullscreen };
constexpr unsigned clampFrameLimit(long long fps) noexcept {
    if (fps <= 0) return 0;
    return static_cast<unsigned>(std::clamp(fps, 15LL, 1000LL));
}
constexpr bool plausibleMode(std::uint32_t a, std::uint32_t b) noexcept {
    const auto small = (std::min)(a, b);
    const auto large = (std::max)(a, b);
    // R_Target stores signed 16-bit dimensions. The driver still determines available modes.
    // Do not reject portrait, 32:9 or multi-monitor aspect ratios.
    return small >= 480 && large <= 32767;
}
inline float correctVerticalFov(float nativeDegrees, float actualAspect, float referenceAspect,
                                bool nativeConversionApplied, float displayAspect = 0) noexcept {
    constexpr double baseline = 16.0 / 9.0;
    constexpr double radians = 3.14159265358979323846 / 180.0;
    if (!std::isfinite(nativeDegrees) || nativeDegrees <= 0 || nativeDegrees >= 179.9f ||
        !std::isfinite(actualAspect) || actualAspect <= 0 ||
        !std::isfinite(referenceAspect) || referenceAspect <= 0) return nativeDegrees;
    // Undo only ApplyProjection's native Vert- conversion, then recreate its 16:9 view.
    // Wide displays retain that vertical view (Hor+); narrower displays gain vertical space.
    const double nativeFactor = nativeConversionApplied ? actualAspect / referenceAspect : 1.0;
    const double baselineFactor = (std::min)(1.0, baseline / referenceAspect);
    if (!std::isfinite(displayAspect) || displayAspect <= 0) displayAspect = actualAspect;
    const double fit = (std::max)(1.0, baseline / displayAspect);
    const double tangent = std::tan(nativeDegrees * radians * 0.5) * baselineFactor * fit / nativeFactor;
    const double result = 2.0 * std::atan(tangent) / radians;
    return std::isfinite(result) && result > 0 && result < 180 ? static_cast<float>(result) : nativeDegrees;
}
inline float correctProjectionAspect(float nativeAspect, float renderAspect, float displayAspect) noexcept {
    if (!std::isfinite(nativeAspect) || nativeAspect <= 0 || !std::isfinite(renderAspect) || renderAspect <= 0 ||
        !std::isfinite(displayAspect) || displayAspect <= 0) return nativeAspect;
    const float result = nativeAspect * displayAspect / renderAspect;
    return std::isfinite(result) && result > 0 ? result : nativeAspect;
}
struct UiViewport { float x, y, width, height; };
inline UiViewport centreUi(UiViewport v, int screenWidth, int screenHeight, float displayAspect = 0) noexcept {
    // Leave sub-rectangles (including split-screen) and already placed movies alone.
    if (v.x != 0 || v.y != 0 || v.width != 1 || v.height != 1 || screenWidth <= 0 || screenHeight <= 0)
        return v;
    constexpr float aspect = 16.0f / 9.0f;
    const float actual = std::isfinite(displayAspect) && displayAspect > 0 ? displayAspect :
        static_cast<float>(screenWidth) / screenHeight;
    if (actual > aspect + 0.01f) {
        const float width = screenWidth * aspect / actual;
        v.x = (screenWidth - width) * 0.5f; // SetViewport's x/y are pixels; w/h are fractions.
        v.width = width / screenWidth;
    } else if (actual < aspect - 0.01f) {
        const float height = screenHeight * actual / aspect;
        v.y = (screenHeight - height) * 0.5f;
        v.height = height / screenHeight;
    }
    return v;
}
inline long scaleCoordinate(long value, unsigned toSize, unsigned fromSize) noexcept {
    if (!toSize || !fromSize) return value;
    const double scaled = static_cast<double>(value) * toSize / fromSize;
    return static_cast<long>(std::clamp(scaled,
        static_cast<double>((std::numeric_limits<long>::min)()),
        static_cast<double>((std::numeric_limits<long>::max)())));
}
inline const wchar_t* modeName(Mode mode) noexcept {
    switch (mode) {
    case Mode::Windowed: return L"windowed";
    case Mode::Fullscreen: return L"fullscreen";
    default: return L"borderless";
    }
}
}
