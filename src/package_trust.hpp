// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <windows.h>
#include <wincrypt.h>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Lj1005::PackageTrust {
    inline constexpr std::string_view FIRMWARE_SHA256 =
        "638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1";
    inline constexpr std::string_view CERTIFICATE_SHA256 =
        "e0ea91f75fad36262a1bf580efe384c7002ab69dd87f6012240a64a600a4eb19";
    inline constexpr std::size_t MAX_PACKAGE_FILE_BYTES = 16 * 1024 * 1024;

    using Bytes = std::vector<BYTE>;
    using Certificate = std::unique_ptr<const CERT_CONTEXT, decltype(&CertFreeCertificateContext)>;

    std::string Hash(std::span<const BYTE> bytes);
    Bytes ReadOrdinaryFile(HANDLE file, std::size_t maximum);
    Bytes ReadOrdinaryFile(const std::filesystem::path& path, std::size_t maximum = MAX_PACKAGE_FILE_BYTES);
    Certificate ReadTesterCertificate(const std::filesystem::path& directory);
    void VerifyCatalog(std::span<const BYTE> catalog, PCCERT_CONTEXT certificate);
    bool CatalogUsesCertificate(std::span<const BYTE> catalog, PCCERT_CONTEXT certificate);
}
