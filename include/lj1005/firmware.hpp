// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include "protocol.hpp"
#include <functional>
#include <string>
#include <string_view>

namespace lj1005 {
    enum class FirmwareState {
        missing,
        ready
    };
    // Rejects unknown models rather than treating every device without FWVER as uninitialized.
    FirmwareState GetFirmwareState(std::string_view ieee1284Id);

    struct FirmwareTransport {
        std::function<std::string()> identify;
        std::function<Bytes()> loadImage;
        std::function<void(std::span<const std::uint8_t>)> write;
        std::function<void()> waitHalfSecond;
    };

    // Returns true only when an upload was needed; readiness is checked after the upload.
    bool EnsureFirmware(const FirmwareTransport& transport);
}
