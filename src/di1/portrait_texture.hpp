// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <string>
#include <string_view>

namespace crabe::di1 {
// Fits the nontransparent top-level image into the DI1 portal's portrait area.
// Accepts legacy DDS A8R8G8B8, DXT1, DXT3 and DXT5; returns a single-level
// A8R8G8B8 DDS with a transparent square canvas. Existing mip levels are ignored.
// Dimensions are limited to 8192 per source axis / 16 Mi source pixels;
// the output size must be 1..4096. Malformed/unsupported input throws runtime_error.
// All nonzero alpha contributes to the bounds, preserving faint character detail.
std::string makePortalPortrait(std::string_view dds, unsigned size = 512,
    bool selected = false);
}
