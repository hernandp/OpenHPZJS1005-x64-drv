// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include <cstddef>
#include <cstdint>

namespace Lj1005::Runtime {
    inline constexpr std::size_t STATUS_BUFFER_BYTES = 4096;
    inline constexpr std::size_t DEVICE_ID_BUFFER_BYTES = 4096;
    inline constexpr std::size_t TRANSFER_CHUNK_BYTES = 65536;
    inline constexpr std::size_t USB_DEVICE_ID_PREFIX_BYTES = 2;
    inline constexpr std::uint32_t FIRMWARE_POLL_MS = 500;
    inline constexpr std::uint32_t FIRMWARE_POLL_ATTEMPTS = 20;
    inline constexpr std::uint32_t COMPLETION_READ_MS = 500;
    inline constexpr std::uint32_t COMPLETION_POLL_MS = 100;
    inline constexpr std::uint32_t SETUP_ECHO_TIMEOUT_MS = 30000;
    inline constexpr std::uint32_t USB_RECONNECT_TIMEOUT_MS = 30000;
    inline constexpr std::uint32_t USB_WRITE_TIMEOUT_MS = 30000;
    inline constexpr std::uint32_t PARALLEL_WRITE_TIMEOUT_MS = 30000;
    inline constexpr std::uint32_t DEVICE_ID_TIMEOUT_MS = 5000;
    inline constexpr std::uint32_t DEFAULT_USB_READ_TIMEOUT_MS = 1000;
    inline constexpr std::uint32_t MAX_USB_READ_TIMEOUT_MS = 10000;
    inline constexpr std::uint64_t COMPLETION_TIMEOUT_MS = 30ull * 60 * 1000;
}
