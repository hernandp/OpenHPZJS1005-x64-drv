// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include "lj1005/runtime_constants.hpp"
#include "protocol.hpp"
#include <string>
#include <stdexcept>

namespace Lj1005 {
    // A read-only USB transport interruption; never permission to replay document data.
    struct UsbReadUnavailable : std::runtime_error {
        using std::runtime_error::runtime_error;
    };

    struct UsbDevice {
        std::wstring path, instance;
    };

    std::vector<UsbDevice> GetUsbDevices();
    std::string GetUsbDeviceId(const UsbDevice& device);
    void UsbSend(const UsbDevice& device, std::span<const std::uint8_t> bytes);
    Bytes UsbRead(const UsbDevice& device, std::uint32_t timeoutMs = Runtime::DEFAULT_USB_READ_TIMEOUT_MS);
}
