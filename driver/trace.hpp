// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include "lj1005/protocol.hpp"
#include <windows.h>
#include <memory>

namespace lj1005 {
    // Registration remains alive until both the monitor instance and its ports close.
    struct TraceRegistration {
        TraceRegistration() noexcept;
        ~TraceRegistration();
    };

    void TraceState(const char* stage, const char* token = "", const char* detail = "", DWORD error = 0) noexcept;
    void TraceStatus(const char* token, std::span<const std::uint8_t> bytes) noexcept;
}
