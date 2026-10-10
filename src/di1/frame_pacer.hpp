// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <windows.h>
#include <cstdint>

namespace crabe::di1 {
class FramePacer {
public:
    ~FramePacer();
    void pace(unsigned limit);
    void reset() noexcept;
private:
    long long frequency{}, next{};
    HANDLE timer{};
};
}
