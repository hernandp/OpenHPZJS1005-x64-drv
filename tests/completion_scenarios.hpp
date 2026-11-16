// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include <cstdint>

namespace CompletionScenario {
    inline constexpr std::uint32_t NORMAL = 0;
    inline constexpr std::uint32_t CANCELLED = 1;
    inline constexpr std::uint32_t READ_FAILURE = 2;
    inline constexpr std::uint32_t RECONNECT = 3;
    inline constexpr std::uint32_t CANCEL_DURING_DISCONNECT = 4;
    inline constexpr std::uint32_t PERSISTENT_DISCONNECT = 5;
}
