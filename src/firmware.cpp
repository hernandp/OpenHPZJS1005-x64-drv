// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/firmware.hpp"
#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace lj1005 {
    namespace {
        std::string Normalize(std::string_view s) {

            std::string result(s);
            std::transform(result.begin(), result.end(), result.begin(),
                           [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

            return result;
        }
    }

    FirmwareState GetFirmwareState(std::string_view id) {
        std::string manufacturer, model, firmware;

        bool seenMfg = false, seenModel = false, seenFirmware = false;
        while (!id.empty()) {
            const auto end = id.find(';');
            const auto field = id.substr(0, end);
            const auto colon = field.find(':');

            if (colon != std::string_view::npos) {
                const auto key = Normalize(field.substr(0, colon));
                const auto value = field.substr(colon + 1);

                if (key == "MFG" || key == "MANUFACTURER") {
                    if (seenMfg)
                        throw std::runtime_error("Duplicate manufacturer in device ID");

                    seenMfg = true;
                    manufacturer = Normalize(value);
                }
                else if (key == "MDL" || key == "MODEL") {
                    if (seenModel)
                        throw std::runtime_error("Duplicate model in device ID");

                    seenModel = true;
                    model = Normalize(value);
                }
                else if (key == "FWVER") {
                    if (seenFirmware)
                        throw std::runtime_error("Duplicate firmware version in device ID");

                    seenFirmware = true;
                    firmware = std::string(value);
                }
            }
            if (end == std::string_view::npos)
                break;

            id.remove_prefix(end + 1);
        }

        if (manufacturer != "HEWLETT-PACKARD" || model != "HP LASERJET 1005 SERIES")
            throw std::runtime_error("Device ID is not the original HP LaserJet 1005 Series");

        return firmware.empty() ? FirmwareState::missing : FirmwareState::ready;
    }

    bool EnsureFirmware(const FirmwareTransport& transport) {

        if (GetFirmwareState(transport.identify()) == FirmwareState::ready)
            return false;

        const auto firmware = WrapFirmware(transport.loadImage());
        transport.write(firmware);

        for (unsigned int attempt = 0; attempt < 20; ++attempt) {
            transport.waitHalfSecond();

            if (GetFirmwareState(transport.identify()) == FirmwareState::ready)
                return true;
        }
        throw std::runtime_error("Printer did not report firmware readiness within 10 seconds");
    }
}
