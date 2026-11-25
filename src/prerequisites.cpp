// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "prerequisites.hpp"
#include "package_trust.hpp"
#include "lj1005/usb.hpp"
#include <windows.h>
#include <cfg.h>
#include <cfgmgr32.h>
#include <shlobj.h>
#include <wincrypt.h>
#include <winhttp.h>
#include <winspool.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <memory>
#include <span>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace Lj1005::Prerequisites {
    namespace {
        namespace Fs = std::filesystem;
        using namespace PackageTrust;

        constexpr std::size_t FIRMWARE_BYTES = 116892;
        constexpr std::size_t MAX_DOWNLOAD_BYTES = 16 * 1024 * 1024;
        constexpr std::size_t DOWNLOAD_BUFFER_BYTES = 16 * 1024;
        constexpr int REQUEST_TIMEOUT_MS = 5000;
        constexpr auto DOWNLOAD_TIMEOUT = std::chrono::seconds(60);
        constexpr DWORD HTTP_OK = 200;
        constexpr DWORD SPOOLER_PORT_LEVEL = 2;
        constexpr wchar_t HP_HOST[] = L"ftp.hp.com";
        constexpr wchar_t HP_PATH[] = L"/pub/softlib/software2/COL2222/lj-10067-2/lj1005hostbased-en.exe";
        constexpr wchar_t CATALOG_NAME[] = L"lj1005.cat";
        constexpr std::wstring_view USB_ID_PREFIX = L"USB\\VID_03F0&PID_1317\\";
        constexpr wchar_t USB_PORTS_KEY[] =
            L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors\\USB Monitor\\Ports\\";
        constexpr std::array STORES{L"Root", L"TrustedPublisher"};

        struct WindowsError : std::system_error {
            WindowsError(const char* action, DWORD code)
                : std::system_error(static_cast<int>(code), std::system_category(), action) {}
        };

        void Require(BOOL success, const char* action) {
            if (!success)
                throw WindowsError(action, GetLastError());
        }

        void CheckCanceled(std::stop_token stop) {
            if (stop.stop_requested())
                throw std::runtime_error("Firmware download canceled.");
        }

        std::wstring Widen(const char* message) {
            const auto length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
            if (!length)
                return L"The operation failed.";
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, message, -1, result.data(), length);
            result.pop_back();
            return result;
        }

        struct CloseHandleGuard {
            HANDLE handle;

            ~CloseHandleGuard() {
                if (handle != INVALID_HANDLE_VALUE)
                    CloseHandle(handle);
            }
        };

        struct Store {
            HCERTSTORE handle;

            ~Store() {
                if (handle)
                    CertCloseStore(handle, 0);
            }
        };

        Certificate VerifyCertificate(const Fs::path& directory) {
            if (!Fs::is_regular_file(directory / INF_NAME))
                throw std::runtime_error("Extract the complete mini ZIP. The driver INF is missing.");
            auto certificate = ReadTesterCertificate(directory);
            if (CertVerifyTimeValidity(nullptr, certificate->pCertInfo))
                throw std::runtime_error("The tester certificate is outside its validity period.");
            VerifyCatalog(ReadOrdinaryFile(directory / CATALOG_NAME), certificate.get());
            return certificate;
        }

        std::vector<std::wstring> MissingStores(PCCERT_CONTEXT certificate) {
            std::vector<std::wstring> missing;
            for (const auto* name : STORES) {
                const Store store{CertOpenStore(
                    CERT_STORE_PROV_SYSTEM_W, 0, 0,
                    CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_READONLY_FLAG | CERT_STORE_OPEN_EXISTING_FLAG, name)};
                if (!store.handle && GetLastError() != static_cast<DWORD>(CRYPT_E_NOT_FOUND) &&
                    GetLastError() != ERROR_FILE_NOT_FOUND)
                    throw WindowsError("Could not read the local computer certificate store", GetLastError());
                Certificate found(store.handle ? CertFindCertificateInStore(store.handle, X509_ASN_ENCODING, 0,
                                                                            CERT_FIND_EXISTING, certificate, nullptr)
                                               : nullptr,
                                  CertFreeCertificateContext);
                if (!found || Hash({found->pbCertEncoded, found->cbCertEncoded}) != CERTIFICATE_SHA256)
                    missing.emplace_back(name);
            }
            return missing;
        }

        void VerifyFirmware(std::span<const BYTE> bytes) {
            if (bytes.size() != FIRMWARE_BYTES || Hash(bytes) != FIRMWARE_SHA256)
                throw std::runtime_error("The image does not match the original HP LaserJet 1005 firmware.");
        }

        struct TemporaryFile {
            Fs::path path;

            ~TemporaryFile() {
                if (!path.empty())
                    DeleteFileW(path.c_str());
            }
        };

        Fs::path UniquePath(const Fs::path& directory, const wchar_t* suffix) {
            GUID id{};
            if (FAILED(CoCreateGuid(&id)))
                throw std::runtime_error("Could not create a temporary file name.");
            wchar_t text[40]{};
            StringFromGUID2(id, text, ARRAYSIZE(text));
            return directory / (std::wstring(text) + suffix);
        }

        void SaveFirmware(const Fs::path& directory, std::span<const BYTE> bytes, std::stop_token stop = {}) {
            VerifyFirmware(bytes);
            CheckCanceled(stop);
            const auto staged = UniquePath(directory, L".tmp");
            TemporaryFile cleanup;
            {
                const CloseHandleGuard file{
                    CreateFileW(staged.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)};
                Require(file.handle != INVALID_HANDLE_VALUE, "Could not write in the extracted driver folder");
                cleanup.path = staged;
                DWORD written = 0;
                Require(WriteFile(file.handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                            written == bytes.size(),
                        "Could not save the verified firmware");
            }
            CheckCanceled(stop);
            const auto output = directory / FIRMWARE_NAME;
            if (!MoveFileW(staged.c_str(), output.c_str())) {
                if (GetLastError() != ERROR_ALREADY_EXISTS && GetLastError() != ERROR_FILE_EXISTS)
                    throw WindowsError("Could not place the verified firmware beside the INF", GetLastError());
                VerifyFirmware(ReadOrdinaryFile(output, FIRMWARE_BYTES));
            }
        }

        struct InternetHandle {
            HINTERNET handle;

            ~InternetHandle() {
                if (handle)
                    WinHttpCloseHandle(handle);
            }
        };

        struct Apartment {
            HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

            ~Apartment() {
                if (SUCCEEDED(result))
                    CoUninitialize();
            }
        };

        bool UsbPortName(std::wstring_view name) {
            return name.starts_with(L"USB") && name.size() > 3 &&
                   std::ranges::all_of(name.substr(3), [](wchar_t c) { return c >= L'0' && c <= L'9'; });
        }
    }

    std::vector<std::wstring> MissingCertificateStores(const Fs::path& directory) {
        return MissingStores(VerifyCertificate(directory).get());
    }

    void InstallTesterCertificate(const Fs::path& directory) {
        const auto certificate = VerifyCertificate(directory);
        for (const auto& name : MissingStores(certificate.get())) {
            const Store store{
                CertOpenStore(CERT_STORE_PROV_SYSTEM_REGISTRY_W, 0, 0, CERT_SYSTEM_STORE_LOCAL_MACHINE, name.c_str())};
            Require(store.handle != nullptr, "Could not open the local computer certificate store for writing");
            Require(
                CertAddCertificateContextToStore(store.handle, certificate.get(), CERT_STORE_ADD_USE_EXISTING, nullptr),
                "Windows could not add the tester certificate");
        }
        if (!MissingStores(certificate.get()).empty())
            throw std::runtime_error("Tester trust is still missing. Check the computer's certificate policy.");
    }

    void SupplyFirmware(const Fs::path& directory, const Fs::path& image) {
        SaveFirmware(directory, ReadOrdinaryFile(image, FIRMWARE_BYTES));
    }

    void ExtractFirmware(const Fs::path& archive, const Fs::path& directory, std::stop_token stop) {
        const Apartment apartment;
        if (FAILED(apartment.result))
            throw std::runtime_error("Could not initialize Windows Shell ZIP support.");
        const auto entry = archive / FIRMWARE_NAME;
        IShellItem* item = nullptr;
        if (FAILED(SHCreateItemFromParsingName(entry.c_str(), nullptr, IID_PPV_ARGS(&item))))
            throw std::runtime_error(
                "Windows could not open sihp1005.img in the HP archive. Browse for the original image.");
        const std::unique_ptr<IShellItem, void (*)(IShellItem*)> itemOwner(item,
                                                                           [](IShellItem* value) { value->Release(); });
        IStream* stream = nullptr;
        if (FAILED(item->BindToHandler(nullptr, BHID_Stream, IID_PPV_ARGS(&stream))))
            throw std::runtime_error("Windows could not read the HP archive. Browse for the original firmware image.");
        const std::unique_ptr<IStream, void (*)(IStream*)> streamOwner(stream,
                                                                       [](IStream* value) { value->Release(); });
        Bytes bytes(FIRMWARE_BYTES + 1);
        std::size_t total = 0;
        while (total < bytes.size()) {
            CheckCanceled(stop);
            ULONG count = 0;
            if (FAILED(stream->Read(bytes.data() + total, static_cast<ULONG>(bytes.size() - total), &count)))
                throw std::runtime_error("Windows could not extract the original HP image.");
            if (!count)
                break;
            total += count;
        }
        bytes.resize(total);
        SaveFirmware(directory, bytes, stop);
    }

    void DownloadFirmware(const Fs::path& directory, std::stop_token stop,
                          const std::function<void(std::size_t)>& progress) {
        const InternetHandle session{
            WinHttpOpen(L"Benteveo prerequisites", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, nullptr, nullptr, 0)};
        Require(session.handle != nullptr, "Could not start WinHTTP");
        Require(WinHttpSetTimeouts(session.handle, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS, REQUEST_TIMEOUT_MS,
                                   REQUEST_TIMEOUT_MS),
                "Could not configure WinHTTP timeouts");
        const InternetHandle connection{WinHttpConnect(session.handle, HP_HOST, INTERNET_DEFAULT_HTTPS_PORT, 0)};
        Require(connection.handle != nullptr, "Could not connect to HP");
        const InternetHandle request{
            WinHttpOpenRequest(connection.handle, L"GET", HP_PATH, nullptr, nullptr, nullptr, WINHTTP_FLAG_SECURE)};
        Require(request.handle != nullptr, "Could not create the HP download request");
        DWORD redirects = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        Require(WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirects, sizeof(redirects)),
                "Could not configure the HP download request");
        CheckCanceled(stop);
        const auto deadline = std::chrono::steady_clock::now() + DOWNLOAD_TIMEOUT;
        Require(WinHttpSendRequest(request.handle, nullptr, 0, nullptr, 0, 0, 0), "Could not request the HP download");
        CheckCanceled(stop);
        Require(WinHttpReceiveResponse(request.handle, nullptr), "Could not receive the HP download");
        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        Require(WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr,
                                    &status, &statusSize, nullptr),
                "Could not read the HP download status");
        if (status != HTTP_OK)
            throw std::runtime_error("HP returned HTTP " + std::to_string(status) +
                                     ". Retry or browse for the original image.");

        const auto temporary = UniquePath(Fs::temp_directory_path(), L".zip");
        TemporaryFile archive;
        {
            const CloseHandleGuard file{CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                                    FILE_ATTRIBUTE_TEMPORARY, nullptr)};
            Require(file.handle != INVALID_HANDLE_VALUE, "Could not create the temporary HP archive");
            archive.path = temporary;
            std::array<BYTE, DOWNLOAD_BUFFER_BYTES> buffer{};
            std::size_t total = 0;
            for (;;) {
                CheckCanceled(stop);
                if (std::chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("The HP download timed out. Retry or browse for the original image.");
                DWORD bytes = 0;
                Require(WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &bytes),
                        "Could not read the HP download");
                if (!bytes)
                    break;
                total += bytes;
                if (total > MAX_DOWNLOAD_BYTES)
                    throw std::runtime_error("The HP download is larger than expected.");
                DWORD written = 0;
                Require(WriteFile(file.handle, buffer.data(), bytes, &written, nullptr) && written == bytes,
                        "Could not save the temporary HP archive");
                progress(total);
            }
        }
        ExtractFirmware(archive.path, directory, stop);
    }

    std::wstring FindPort() {
        std::vector<std::wstring> ids;
        for (auto device : GetUsbDevices()) {
            auto& id = device.instance;
            if (id.size() <= USB_ID_PREFIX.size() ||
                CompareStringOrdinal(id.data(), static_cast<int>(USB_ID_PREFIX.size()), USB_ID_PREFIX.data(),
                                     static_cast<int>(USB_ID_PREFIX.size()), TRUE) != CSTR_EQUAL)
                continue;
            DEVINST node = 0;
            ULONG status = 0;
            ULONG problem = 0;
            if (CM_Locate_DevNodeW(&node, id.data(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS &&
                CM_Get_DevNode_Status(&status, &problem, node, 0) == CR_SUCCESS && (status & DN_STARTED) &&
                !(status & DN_HAS_PROBLEM))
                ids.push_back(id);
        }
        std::ranges::sort(ids);
        ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
        if (ids.empty())
            throw std::runtime_error("Connect the original HP LaserJet 1005, turn it on, and retry.");
        if (ids.size() != 1)
            throw std::runtime_error("Leave only the LaserJet 1005 you want to install connected, then retry.");

        DWORD needed = 0;
        DWORD count = 0;
        if (EnumPortsW(nullptr, SPOOLER_PORT_LEVEL, nullptr, 0, &needed, &count) || !needed)
            throw std::runtime_error("Windows has not exposed a USB print port. Check the Print Spooler and retry.");
        Bytes buffer(needed);
        Require(EnumPortsW(nullptr, SPOOLER_PORT_LEVEL, buffer.data(), needed, &needed, &count),
                "Could not list Windows print ports");
        const auto* ports = reinterpret_cast<PORT_INFO_2W*>(buffer.data());
        std::wstring match;
        for (DWORD index = 0; index < count; ++index) {
            const auto* name = ports[index].pPortName;
            if (!name || !UsbPortName(name))
                continue;
            wchar_t id[MAX_DEVICE_ID_LEN]{};
            DWORD size = sizeof(id);
            const auto key = std::wstring(USB_PORTS_KEY) + name;
            if (RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Device Id", RRF_RT_REG_SZ, nullptr, id, &size) !=
                    ERROR_SUCCESS ||
                CompareStringOrdinal(id, -1, ids.front().c_str(), -1, TRUE) != CSTR_EQUAL)
                continue;
            if (!match.empty())
                throw std::runtime_error("More than one USB port matches. Reconnect the printer and retry.");
            match = name;
        }
        if (match.empty())
            throw std::runtime_error(
                "Windows sees the printer but has not exposed its matching USB port. Reconnect and retry.");
        return match;
    }

    Status Inspect(const Fs::path& directory) {
        Status status;
        try {
            status.missingStores = MissingCertificateStores(directory);
            status.certificate = status.missingStores.empty() ? Check{true, L"Tester trust is installed."}
                                                              : Check{false, L"Tester trust must be installed."};
        }
        catch (const std::exception& error) {
            status.certificate.detail = Widen(error.what());
        }
        try {
            VerifyFirmware(ReadOrdinaryFile(directory / FIRMWARE_NAME, FIRMWARE_BYTES));
            status.firmware = {true, L"The original sihp1005.img is verified."};
        }
        catch (const std::exception&) {
            std::error_code error;
            const auto file = Fs::symlink_status(directory / FIRMWARE_NAME, error);
            status.firmware.detail = file.type() == Fs::file_type::not_found
                                         ? L"sihp1005.img is missing."
                                         : L"Cannot verify sihp1005.img. Move it aside before replacing it.";
        }
        try {
            status.port = FindPort();
            status.usb = {true, L"Select " + status.port + L" in Have Disk."};
        }
        catch (const std::exception& error) {
            status.usb.detail = Widen(error.what());
        }
        return status;
    }
}
