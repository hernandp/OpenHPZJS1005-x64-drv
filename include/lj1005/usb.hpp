// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include "protocol.hpp"
#include <string>

namespace lj1005 {
    struct UsbDevice {
        std::wstring path, instance;
    };

    std::vector<UsbDevice> GetUsbDevices();
    std::string GetUsbDeviceId(const UsbDevice& device);
    void UsbSend(const UsbDevice& device, std::span<const std::uint8_t> bytes);
    Bytes UsbRead(const UsbDevice& device, std::uint32_t timeoutMs = 1000);
}
