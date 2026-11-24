// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <stop_token>
#include <string>
#include <vector>

namespace Lj1005::Prerequisites {
    inline constexpr wchar_t CERTIFICATE_THUMBPRINT[] = L"DEAD78F5D4F2A4D8A424726555A6C3D536E445FC";
    inline constexpr wchar_t INF_NAME[] = L"lj1005.inf";
    inline constexpr wchar_t FIRMWARE_NAME[] = L"sihp1005.img";

    struct Check {
        bool ready = false;
        std::wstring detail;
    };

    struct Status {
        Check certificate;
        Check firmware;
        Check usb;
        std::vector<std::wstring> missingStores;
        std::wstring port;

        bool Ready() const {
            return certificate.ready && firmware.ready && usb.ready;
        }
    };

    Status Inspect(const std::filesystem::path& directory);
    std::vector<std::wstring> MissingCertificateStores(const std::filesystem::path& directory);
    void InstallTesterCertificate(const std::filesystem::path& directory);
    void SupplyFirmware(const std::filesystem::path& directory, const std::filesystem::path& image);
    void ExtractFirmware(const std::filesystem::path& archive, const std::filesystem::path& directory,
                         std::stop_token stop);
    void DownloadFirmware(const std::filesystem::path& directory, std::stop_token stop,
                          const std::function<void(std::size_t)>& progress);
    std::wstring FindPort();
}
