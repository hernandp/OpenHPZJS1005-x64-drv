// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "package_trust.hpp"
#include <bcrypt.h>
#include <array>
#include <stdexcept>
#include <system_error>

namespace Lj1005::PackageTrust {
    namespace {
        namespace Fs = std::filesystem;
        constexpr std::size_t SHA256_BYTES = 32;

        void Require(BOOL success, const char* action) {
            if (!success)
                throw std::system_error(static_cast<int>(GetLastError()), std::system_category(), action);
        }

        struct CloseHandleGuard {
            HANDLE handle;

            ~CloseHandleGuard() {
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }
        };

        struct CatalogMessage {
            HCRYPTMSG handle = nullptr;

            explicit CatalogMessage(std::span<const BYTE> catalog) {
                CRYPT_DATA_BLOB blob{static_cast<DWORD>(catalog.size()), const_cast<BYTE*>(catalog.data())};
                Require(CryptQueryObject(CERT_QUERY_OBJECT_BLOB, &blob, CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED,
                                         CERT_QUERY_FORMAT_FLAG_BINARY, 0, nullptr, nullptr, nullptr, nullptr, &handle,
                                         nullptr),
                        "Could not open the driver catalog");
            }

            ~CatalogMessage() {
                CryptMsgClose(handle);
            }

            DWORD SignerCount() const {
                DWORD count = 0;
                DWORD size = sizeof(count);
                Require(CryptMsgGetParam(handle, CMSG_SIGNER_COUNT_PARAM, 0, &count, &size),
                        "Could not inspect the driver catalog");
                if (!count)
                    throw std::runtime_error("The driver catalog has no signer.");
                return count;
            }

            bool Matches(DWORD index, PCCERT_CONTEXT certificate) const {
                DWORD size = 0;
                Require(CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, nullptr, &size),
                        "Could not inspect the catalog signer");
                Bytes signer(size);
                Require(CryptMsgGetParam(handle, CMSG_SIGNER_CERT_INFO_PARAM, index, signer.data(), &size),
                        "Could not read the catalog signer");
                return CertCompareCertificate(X509_ASN_ENCODING, reinterpret_cast<CERT_INFO*>(signer.data()),
                                              certificate->pCertInfo);
            }
        };
    }

    std::string Hash(std::span<const BYTE> bytes) {
        BCRYPT_ALG_HANDLE algorithm = nullptr;
        if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
            throw std::runtime_error("Could not open Windows SHA-256.");
        std::array<BYTE, SHA256_BYTES> digest{};
        const auto result =
            BCryptHash(algorithm, nullptr, 0, const_cast<BYTE*>(bytes.data()), static_cast<ULONG>(bytes.size()),
                       digest.data(), static_cast<ULONG>(digest.size()));
        BCryptCloseAlgorithmProvider(algorithm, 0);
        if (result < 0)
            throw std::runtime_error("Could not verify the SHA-256 digest.");
        constexpr char HEX[] = "0123456789abcdef";
        std::string hash;
        for (const BYTE byte : digest) {
            hash += HEX[byte >> 4];
            hash += HEX[byte & 0xf];
        }
        return hash;
    }

    Bytes ReadOrdinaryFile(HANDLE file, std::size_t maximum) {
        BY_HANDLE_FILE_INFORMATION info{};
        Require(GetFileInformationByHandle(file, &info), "Could not inspect the required file");
        if (info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT) || info.nFileSizeHigh ||
            info.nFileSizeLow > maximum)
            throw std::runtime_error("The file has an unexpected type or size.");
        Bytes bytes(info.nFileSizeLow);
        DWORD read = 0;
        Require(ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr),
                "Could not read the required file");
        if (read != bytes.size())
            throw std::runtime_error("The file could not be read completely.");
        return bytes;
    }

    Bytes ReadOrdinaryFile(const Fs::path& path, std::size_t maximum) {
        const CloseHandleGuard file{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        Require(file.handle != INVALID_HANDLE_VALUE, "Could not open the required file");
        return ReadOrdinaryFile(file.handle, maximum);
    }

    Certificate ReadTesterCertificate(const Fs::path& directory) {
        const auto bytes = ReadOrdinaryFile(directory / L"TEST-CERTIFICATE.cer");
        if (Hash(bytes) != CERTIFICATE_SHA256)
            throw std::runtime_error("The tester certificate differs from the Benteveo signing identity.");
        Certificate certificate(
            CertCreateCertificateContext(X509_ASN_ENCODING, bytes.data(), static_cast<DWORD>(bytes.size())),
            CertFreeCertificateContext);
        Require(certificate != nullptr, "Could not decode the tester certificate");
        return certificate;
    }

    void VerifyCatalog(std::span<const BYTE> catalog, PCCERT_CONTEXT certificate) {
        const CatalogMessage message(catalog);
        if (message.SignerCount() != 1 || !message.Matches(0, certificate))
            throw std::runtime_error("The catalog signer differs from the Benteveo signing identity.");
        Require(CryptMsgControl(message.handle, 0, CMSG_CTRL_VERIFY_SIGNATURE, certificate->pCertInfo),
                "The driver catalog signature could not be verified");
    }

    bool CatalogUsesCertificate(std::span<const BYTE> catalog, PCCERT_CONTEXT certificate) {
        const CatalogMessage message(catalog);
        const auto count = message.SignerCount();
        for (DWORD index = 0; index < count; ++index) {
            if (message.Matches(index, certificate))
                return true;
        }
        return false;
    }
}
