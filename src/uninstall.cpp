// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "uninstall.hpp"
#include "package_trust.hpp"
#include <aclapi.h>
#include <bcrypt.h>
#include <mscat.h>
#include <setupapi.h>
#include <winspool.h>
#include <algorithm>
#include <array>
#include <cwchar>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace Lj1005::Uninstall {
    namespace {
        namespace Fs = std::filesystem;
        using namespace PackageTrust;

        constexpr wchar_t MODEL[] = L"HP Laserjet 1005 (Project Benteveo Driver)";
        constexpr wchar_t MONITOR[] = L"HP LaserJet 1005 Firmware Monitor (Project Benteveo)";
        constexpr wchar_t ENVIRONMENT[] = L"Windows x64";
        constexpr wchar_t ALL_ENVIRONMENTS[] = L"all";
        constexpr wchar_t INF_NAME[] = L"lj1005.inf";
        constexpr wchar_t CATALOG[] = L"lj1005.cat";
        constexpr std::wstring_view PACKAGE_VERSION = BENTEVEO_PACKAGE_VERSION;
        constexpr wchar_t MONITOR_DLL[] = L"lj1005monitor.dll";
        constexpr wchar_t INSTALLED_FIRMWARE[] = L"lj1005-firmware.img";
        constexpr wchar_t MONITOR_KEY[] = L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors";
        constexpr std::array MODELS{MODEL, L"HP Laserjet 1005 (Benteveo Driver)",
                                    L"HP LaserJet 1005 Series (Open ZJS)"};
        constexpr std::array PROVIDERS{L"Project Benteveo", L"Benteveo Project", L"Open ZJS Project"};
        constexpr std::array DRIVER_FILES{L"lj1005.gpd", L"lj1005.ini", L"lj1005render.dll", L"lj1005ui.dll"};
        constexpr std::array PACKAGE_FILES{INF_NAME,        MONITOR_DLL,     DRIVER_FILES[0],
                                           DRIVER_FILES[1], DRIVER_FILES[2], DRIVER_FILES[3]};
        constexpr std::array STORES{L"TrustedPublisher", L"Root"};
        constexpr DWORD PRINTER_LEVEL = 2;
        constexpr DWORD DRIVER_LEVEL = 8;

        [[noreturn]] void WindowsError(const char* action, DWORD error = GetLastError()) {
            throw std::system_error(static_cast<int>(error), std::system_category(), action);
        }

        void Require(BOOL success, const char* action) {
            if (!success)
                WindowsError(action);
        }

        void CheckStatus(DWORD error, const char* action) {
            if (error != ERROR_SUCCESS)
                WindowsError(action, error);
        }

        bool Equal(std::wstring_view left, std::wstring_view right) {
            return CompareStringOrdinal(left.data(), static_cast<int>(left.size()), right.data(),
                                        static_cast<int>(right.size()), TRUE) == CSTR_EQUAL;
        }

        bool Equal(const wchar_t* left, std::wstring_view right) {
            return left && Equal(std::wstring_view(left), right);
        }

        struct Handle {
            HANDLE value;

            ~Handle() {
                if (value && value != INVALID_HANDLE_VALUE)
                    CloseHandle(value);
            }
        };

        struct Key {
            HKEY value = nullptr;

            ~Key() {
                if (value)
                    RegCloseKey(value);
            }
        };

        struct Inf {
            HINF handle;

            explicit Inf(const Fs::path& path)
                : handle(SetupOpenInfFileW(path.c_str(), nullptr, INF_STYLE_WIN4, nullptr)) {
                Require(handle != INVALID_HANDLE_VALUE, "Could not open a driver INF");
            }

            ~Inf() {
                SetupCloseInfFile(handle);
            }

            std::wstring VersionField(const wchar_t* key, DWORD field) const {
                INFCONTEXT context{};
                if (!SetupFindFirstLineW(handle, L"Version", key, &context))
                    return {};
                DWORD length = 0;
                SetupGetStringFieldW(&context, field, nullptr, 0, &length);
                if (!length)
                    return {};
                std::wstring value(length, L'\0');
                Require(SetupGetStringFieldW(&context, field, value.data(), length, nullptr),
                        "Could not read an INF field");
                value.resize(length - 1);
                return value;
            }
        };

        struct Catalog {
            HANDLE handle;
            HCATADMIN admin = nullptr;

            explicit Catalog(const Fs::path& path)
                : handle(CryptCATOpen(const_cast<wchar_t*>(path.c_str()), CRYPTCAT_OPEN_EXISTING, 0, 0, 0)) {
                Require(handle != INVALID_HANDLE_VALUE, "Could not inspect the catalog members");
                if (!CryptCATAdminAcquireContext2(&admin, nullptr, BCRYPT_SHA256_ALGORITHM, nullptr, 0)) {
                    const auto error = GetLastError();
                    CryptCATClose(handle);
                    WindowsError("Could not initialize catalog hashing", error);
                }
            }

            ~Catalog() {
                CryptCATAdminReleaseContext(admin, 0);
                CryptCATClose(handle);
            }

            void VerifyMember(HANDLE file) const {
                DWORD size = 0;
                Require(CryptCATAdminCalcHashFromFileHandle2(admin, file, &size, nullptr, 0),
                        "Could not size a catalog file hash");
                Bytes hash(size);
                Require(size && CryptCATAdminCalcHashFromFileHandle2(admin, file, &size, hash.data(), 0),
                        "Could not calculate a catalog file hash");
                constexpr wchar_t HEX[] = L"0123456789ABCDEF";
                std::wstring tag;
                for (const auto byte : hash) {
                    tag += HEX[byte >> 4];
                    tag += HEX[byte & 0xf];
                }
                if (!CryptCATGetMemberInfo(handle, tag.data()))
                    throw std::runtime_error(
                        "A reference file does not match the signed catalog. Extract a fresh copy of the matching ZIP.");
            }
        };

        Handle OpenFile(const Fs::path& path, DWORD access = GENERIC_READ) {
            return {CreateFileW(path.c_str(), access, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
        }

        struct Reference {
            Certificate certificate{nullptr, CertFreeCertificateContext};
            std::string catalogHash;
            std::array<std::string, PACKAGE_FILES.size()> hashes;
        };

        Reference ReadReference(const Fs::path& directory) {
            Reference reference;
            reference.certificate = ReadTesterCertificate(directory);
            const auto path = directory / CATALOG;
            const auto file = OpenFile(path);
            Require(file.value != INVALID_HANDLE_VALUE, "Could not open the reference catalog");
            const auto bytes = ReadOrdinaryFile(file.value, MAX_PACKAGE_FILE_BYTES);
            VerifyCatalog(bytes, reference.certificate.get());
            reference.catalogHash = Hash(bytes);
            const Catalog catalog(path);
            for (std::size_t index = 0; index < PACKAGE_FILES.size(); ++index) {
                const auto member = OpenFile(directory / PACKAGE_FILES[index]);
                Require(member.value != INVALID_HANDLE_VALUE, "A required reference file is missing or in use");
                reference.hashes[index] = Hash(ReadOrdinaryFile(member.value, MAX_PACKAGE_FILE_BYTES));
                catalog.VerifyMember(member.value);
                if (index == 0 && Inf(directory / INF_NAME).VersionField(L"DriverVer", 2) != PACKAGE_VERSION)
                    throw std::runtime_error(
                        "These driver files are from a different release. Run the uninstaller inside its original extracted ZIP.");
            }
            return reference;
        }

        void AssertHash(const Fs::path& path, std::string_view hash) {
            if (Hash(ReadOrdinaryFile(path)) != hash)
                throw std::runtime_error(
                    "A file differs from this build. Use the original extracted ZIP for the installed build or follow the manual removal guide.");
        }

        struct Locations {
            Fs::path system;
            Fs::path inf;
            Fs::path repository;

            Locations() {
                wchar_t path[MAX_PATH]{};
                auto length = GetSystemDirectoryW(path, ARRAYSIZE(path));
                Require(length && length < ARRAYSIZE(path), "Could not locate the system directory");
                system = path;
                length = GetSystemWindowsDirectoryW(path, ARRAYSIZE(path));
                Require(length && length < ARRAYSIZE(path), "Could not locate the Windows directory");
                inf = Fs::path(path) / L"INF";
                repository = system / L"DriverStore" / L"FileRepository";
            }
        };

        bool PublishedName(std::wstring_view name) {
            return name.size() > std::wstring_view(L"oem.inf").size() && Equal(name.substr(0, 3), L"oem") &&
                   Equal(name.substr(name.size() - 4), L".inf") &&
                   name.substr(3, name.size() - 7).find_first_not_of(L"0123456789") == std::wstring_view::npos;
        }

        Fs::path StoreInf(const Locations& locations, const Fs::path& published) {
            wchar_t path[MAX_PATH]{};
            Require(
                SetupGetInfDriverStoreLocationW(published.c_str(), nullptr, nullptr, path, ARRAYSIZE(path), nullptr),
                "Could not resolve a published Driver Store package");
            const Fs::path store = Fs::path(path).lexically_normal();
            if (!Equal(store.parent_path().parent_path().native(), locations.repository.native()))
                throw std::runtime_error("The published driver has an unexpected Driver Store location.");
            return store;
        }

        Fs::path DetectPackage(const Locations& locations, const Reference& reference, State& state) {
            Fs::path result;
            for (const auto& entry : Fs::directory_iterator(locations.inf)) {
                const auto published = entry.path().filename().native();
                if (!PublishedName(published))
                    continue;
                const auto store = StoreInf(locations, entry.path());
                const Inf inf(store);
                const auto provider = inf.VersionField(L"Provider", 1);
                if (!Equal(store.filename().native(), INF_NAME) &&
                    std::ranges::find(PROVIDERS, provider) == PROVIDERS.end())
                    continue;
                if (!result.empty())
                    throw std::runtime_error("Multiple Benteveo packages are staged. Use the manual removal guide.");
                if (!Equal(provider, PROVIDERS.front()) || !Equal(inf.VersionField(L"Class", 1), L"Printer") ||
                    !Equal(store.filename().native(), INF_NAME))
                    throw std::runtime_error(
                        "An unrecognized Benteveo package is staged. Use the manual removal guide.");
                if (inf.VersionField(L"DriverVer", 2) != PACKAGE_VERSION)
                    throw std::runtime_error(
                        "Windows has a different Benteveo release installed. Use the uninstaller from that release's ZIP or follow the manual removal guide.");
                AssertHash(entry.path(), reference.hashes.front());
                AssertHash(store.parent_path() / CATALOG, reference.catalogHash);
                for (std::size_t index = 0; index < PACKAGE_FILES.size(); ++index)
                    AssertHash(store.parent_path() / PACKAGE_FILES[index], reference.hashes[index]);
                state.publishedInf = published;
                result = store;
            }
            return result;
        }

        template <typename Info, typename Enumerate, typename Inspect>
        void SpoolerInventory(Enumerate enumerate, Inspect inspect) {
            DWORD bytes = 0;
            DWORD count = 0;
            if (enumerate(nullptr, 0, &bytes, &count))
                return;
            if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
                WindowsError("Could not enumerate Windows printing components");
            Bytes buffer(bytes);
            Require(enumerate(buffer.data(), bytes, &bytes, &count), "Could not enumerate Windows printing components");
            const auto* entries = reinterpret_cast<const Info*>(buffer.data());
            for (DWORD index = 0; index < count; ++index)
                inspect(entries[index]);
        }

        void InspectDrivers(const Locations& locations, const Fs::path& store, const Reference& reference,
                            State& state) {
            SpoolerInventory<DRIVER_INFO_8W>(
                [](BYTE* buffer, DWORD size, DWORD* required, DWORD* count) {
                    return EnumPrinterDriversW(nullptr, const_cast<wchar_t*>(ALL_ENVIRONMENTS), DRIVER_LEVEL, buffer,
                                               size, required, count);
                },
                [&](const DRIVER_INFO_8W& driver) {
                    std::vector<Fs::path> files;
                    for (const auto* path :
                         {driver.pDataFile, driver.pConfigFile, driver.pDriverPath, driver.pszInfPath})
                        if (path)
                            files.emplace_back(path);
                    for (const auto* path = driver.pDependentFiles; path && *path; path += std::wcslen(path) + 1)
                        files.emplace_back(path);
                    const bool relevant = std::ranges::any_of(files, [](const auto& file) {
                        return std::ranges::any_of(
                                   PACKAGE_FILES,
                                   [&](const auto* name) { return Equal(file.filename().native(), name); }) ||
                               Equal(file.filename().native(), INSTALLED_FIRMWARE);
                    });
                    if (!relevant && !Equal(driver.pMonitorName, MONITOR) &&
                        !std::ranges::any_of(MODELS, [&](const auto* model) { return Equal(driver.pName, model); }))
                        return;
                    if (state.driver || store.empty() || !Equal(driver.pName, MODEL) ||
                        !Equal(driver.pEnvironment, ENVIRONMENT) || !Equal(driver.pMonitorName, MONITOR) ||
                        !Equal(driver.pszInfPath, store.native()))
                        throw std::runtime_error(
                            "A registered driver refers to Benteveo and does not match the reference. Use the manual removal guide.");
                    for (const auto* name : DRIVER_FILES) {
                        Fs::path found;
                        for (const auto& file : files) {
                            if (!Equal(file.filename().native(), name))
                                continue;
                            if (!found.empty() && !Equal(found.native(), file.native()))
                                throw std::runtime_error("An installed driver file has multiple locations.");
                            const auto parent = file.parent_path().lexically_normal();
                            if (!Equal(parent.native(), (locations.system / L"spool\\drivers\\x64\\3").native()) &&
                                !Equal(parent.native(), store.parent_path().native()))
                                throw std::runtime_error("An installed driver file has an unexpected location.");
                            found = file;
                        }
                        if (found.empty())
                            throw std::runtime_error("An installed Benteveo driver file could not be identified.");
                        const auto index =
                            std::ranges::find(PACKAGE_FILES, std::wstring_view(name)) - PACKAGE_FILES.begin();
                        AssertHash(found, reference.hashes[static_cast<std::size_t>(index)]);
                    }
                    state.driver = true;
                });
        }

        void InspectQueues(State& state) {
            SpoolerInventory<PRINTER_INFO_2W>(
                [](BYTE* buffer, DWORD size, DWORD* required, DWORD* count) {
                    return EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, PRINTER_LEVEL, buffer, size, required, count);
                },
                [&](const PRINTER_INFO_2W& printer) {
                    if (!std::ranges::any_of(MODELS,
                                             [&](const auto* model) { return Equal(printer.pDriverName, model); }))
                        return;
                    if (!state.driver || !Equal(printer.pDriverName, MODEL) || !printer.pPrinterName ||
                        !printer.pPortName || !(printer.Attributes & PRINTER_ATTRIBUTE_LOCAL))
                        throw std::runtime_error("A printer queue is outside the verified local driver package.");
                    state.queues.push_back({printer.pPrinterName, printer.pPortName, printer.cJobs});
                });
            std::ranges::sort(state.queues, {}, &Queue::name);
        }

        bool InspectMonitors() {
            Key monitors;
            CheckStatus(RegOpenKeyExW(HKEY_LOCAL_MACHINE, MONITOR_KEY, 0, KEY_READ, &monitors.value),
                        "Could not inspect registered print monitors");
            bool found = false;
            for (DWORD index = 0;; ++index) {
                wchar_t name[MAX_PATH]{};
                DWORD length = ARRAYSIZE(name);
                const auto status =
                    RegEnumKeyExW(monitors.value, index, name, &length, nullptr, nullptr, nullptr, nullptr);
                if (status == ERROR_NO_MORE_ITEMS)
                    break;
                CheckStatus(status, "Could not enumerate registered print monitors");
                wchar_t driver[MAX_PATH]{};
                DWORD bytes = sizeof(driver);
                CheckStatus(RegGetValueW(monitors.value, name, L"Driver", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr,
                                         driver, &bytes),
                            "Could not read a monitor registration");
                if (Equal(name, MONITOR)) {
                    if (!Equal(driver, MONITOR_DLL))
                        throw std::runtime_error("The registered Benteveo monitor points to an unexpected file.");
                    found = true;
                }
                else if (Equal(Fs::path(driver).filename().native(), MONITOR_DLL))
                    throw std::runtime_error(
                        "Another print monitor uses the Benteveo monitor DLL. Use the manual removal guide.");
            }
            return found;
        }

        struct Store {
            HCERTSTORE handle;

            Store(const wchar_t* name, bool readOnly)
                : handle(CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                                       CERT_SYSTEM_STORE_LOCAL_MACHINE | CERT_STORE_OPEN_EXISTING_FLAG |
                                           (readOnly ? CERT_STORE_READONLY_FLAG : 0),
                                       name)) {
                if (!handle && GetLastError() != ERROR_FILE_NOT_FOUND &&
                    GetLastError() != static_cast<DWORD>(CRYPT_E_NOT_FOUND))
                    WindowsError("Could not open the local machine certificate store");
            }

            ~Store() {
                if (handle)
                    CertCloseStore(handle, 0);
            }

            Certificate Find(PCCERT_CONTEXT certificate) const {
                if (!handle)
                    return {nullptr, CertFreeCertificateContext};
                auto* found =
                    CertFindCertificateInStore(handle, X509_ASN_ENCODING, 0, CERT_FIND_EXISTING, certificate, nullptr);
                if (!found && GetLastError() != static_cast<DWORD>(CRYPT_E_NOT_FOUND))
                    WindowsError("Could not check the local machine certificate store");
                Certificate result(found, CertFreeCertificateContext);
                if (result && Hash({result->pbCertEncoded, result->cbCertEncoded}) != CERTIFICATE_SHA256)
                    throw std::runtime_error(
                        "A different certificate has the same issuer and serial number. Tester trust was retained.");
                return result;
            }
        };

        State Inspect(const Locations& locations, const Reference& reference) {
            State state;
            state.version = PACKAGE_VERSION;
            state.catalogHash = reference.catalogHash;
            const auto store = DetectPackage(locations, reference, state);
            InspectDrivers(locations, store, reference, state);
            InspectQueues(state);
            state.monitor = InspectMonitors();
            for (const auto* name : {MONITOR_DLL, INSTALLED_FIRMWARE}) {
                const auto path = locations.system / name;
                const auto attributes = GetFileAttributesW(path.c_str());
                if (attributes == INVALID_FILE_ATTRIBUTES) {
                    if (GetLastError() == ERROR_FILE_NOT_FOUND)
                        continue;
                    WindowsError("Could not inspect an installed Benteveo file");
                }
                AssertHash(path, Equal(name, MONITOR_DLL) ? reference.hashes[1] : FIRMWARE_SHA256);
                state.files.push_back(path);
            }
            state.testerTrust = std::ranges::any_of(STORES, [&](const auto* name) {
                return Store(name, true).Find(reference.certificate.get()) != nullptr;
            });
            return state;
        }

        struct Privilege {
            Handle token{nullptr};
            TOKEN_PRIVILEGES previous{};

            explicit Privilege(const wchar_t* name) {
                Require(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token.value),
                        "Could not open the administrator token");
                TOKEN_PRIVILEGES requested{};
                requested.PrivilegeCount = 1;
                requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                Require(LookupPrivilegeValueW(nullptr, name, &requested.Privileges[0].Luid),
                        "Could not find a Windows privilege");
                DWORD length = 0;
                Require(AdjustTokenPrivileges(token.value, FALSE, &requested, sizeof(previous), &previous, &length),
                        "Could not enable a Windows privilege");
                CheckStatus(GetLastError(), "Could not enable a Windows privilege");
            }

            ~Privilege() {
                AdjustTokenPrivileges(token.value, FALSE, &previous, 0, nullptr, nullptr);
            }
        };

        struct Printer {
            HANDLE handle = nullptr;

            explicit Printer(const std::wstring& name) {
                PRINTER_DEFAULTSW defaults{};
                // DeletePrinter checks the DELETE right on the opened handle.
                defaults.DesiredAccess = PRINTER_ACCESS_ADMINISTER | PRINTER_ACCESS_USE | DELETE;
                Require(OpenPrinterW(const_cast<wchar_t*>(name.c_str()), &handle, &defaults),
                        "Could not open a confirmed printer queue");
            }

            ~Printer() {
                ClosePrinter(handle);
            }
        };

        void RemoveQueue(const Queue& confirmed) {
            const Printer printer(confirmed.name);
            DWORD bytes = 0;
            GetPrinterW(printer.handle, PRINTER_LEVEL, nullptr, 0, &bytes);
            Bytes buffer(bytes);
            Require(GetPrinterW(printer.handle, PRINTER_LEVEL, buffer.data(), bytes, &bytes),
                    "Could not recheck a printer queue");
            const auto& info = *reinterpret_cast<const PRINTER_INFO_2W*>(buffer.data());
            if (!Equal(info.pDriverName, MODEL) || !Equal(info.pPortName, confirmed.port) || info.cJobs)
                throw std::runtime_error(
                    "A queue changed or received a print job. Close printing apps and review removal again.");
            Require(DeletePrinter(printer.handle), "Could not remove a verified printer queue");
        }

        void RemovePackage(const Locations& locations, const std::wstring& published) {
            const auto executable = locations.system / L"pnputil.exe";
            auto command = L"pnputil.exe /delete-driver " + published;
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION process{};
            Require(CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                                   nullptr, locations.system.c_str(), &startup, &process),
                    "Could not start PnPUtil");
            const Handle child{process.hProcess};
            const Handle thread{process.hThread};
            Require(WaitForSingleObject(child.value, INFINITE) == WAIT_OBJECT_0, "Could not wait for PnPUtil");
            DWORD exitCode = 0;
            Require(GetExitCodeProcess(child.value, &exitCode), "Could not read the PnPUtil result");
            CheckStatus(exitCode, "PnPUtil could not finish removing the Driver Store package");
        }

        void DeleteVerifiedFile(const Fs::path& path, std::string_view hash) {
            const auto file = OpenFile(path, GENERIC_READ | DELETE);
            Require(file.value != INVALID_HANDLE_VALUE, "Could not open the verified System32 file for deletion");
            if (Hash(ReadOrdinaryFile(file.value, MAX_PACKAGE_FILE_BYTES)) != hash)
                throw std::runtime_error("The installed file changed during cleanup.");
            FILE_DISPOSITION_INFO remove{TRUE};
            Require(SetFileInformationByHandle(file.value, FileDispositionInfo, &remove, sizeof(remove)),
                    "Could not delete the verified System32 file");
        }

        void RemoveFile(const Fs::path& path, std::string_view hash) {
            try {
                DeleteVerifiedFile(path, hash);
                return;
            }
            catch (const std::system_error& error) {
                if (error.code().value() != ERROR_ACCESS_DENIED)
                    throw;
            }

            AssertHash(path, hash);
            PSID owner = nullptr;
            PACL acl = nullptr;
            PSECURITY_DESCRIPTOR descriptor = nullptr;
            auto mutablePath = path.native();
            CheckStatus(GetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT,
                                              OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION, &owner, nullptr,
                                              &acl, nullptr, &descriptor),
                        "Could not save the verified file permissions");
            const std::unique_ptr<void, decltype(&LocalFree)> saved(descriptor, LocalFree);
            BYTE administrators[SECURITY_MAX_SID_SIZE]{};
            DWORD sidSize = sizeof(administrators);
            Require(CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, administrators, &sidSize),
                    "Could not identify local administrators");
            EXPLICIT_ACCESSW access{};
            access.grfAccessPermissions = DELETE | GENERIC_READ;
            access.grfAccessMode = GRANT_ACCESS;
            BuildTrusteeWithSidW(&access.Trustee, administrators);
            PACL replacement = nullptr;
            CheckStatus(SetEntriesInAclW(1, &access, acl, &replacement), "Could not prepare file-specific permissions");
            const std::unique_ptr<void, decltype(&LocalFree)> updated(replacement, LocalFree);
            SECURITY_DESCRIPTOR_CONTROL control = 0;
            DWORD revision = 0;
            Require(GetSecurityDescriptorControl(descriptor, &control, &revision),
                    "Could not inspect saved permissions");
            const auto protection = (control & SE_DACL_PROTECTED) ? PROTECTED_DACL_SECURITY_INFORMATION
                                                                  : UNPROTECTED_DACL_SECURITY_INFORMATION;
            const Privilege restore(SE_RESTORE_NAME);
            CheckStatus(SetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                              administrators, nullptr, nullptr, nullptr),
                        "Could not take ownership of the verified file");
            try {
                CheckStatus(SetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT,
                                                  DACL_SECURITY_INFORMATION | protection, nullptr, nullptr, replacement,
                                                  nullptr),
                            "Could not grant file-specific removal access");
                DeleteVerifiedFile(path, hash);
            }
            catch (...) {
                CheckStatus(
                    SetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT,
                                          OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | protection, owner,
                                          nullptr, acl, nullptr),
                    "Removal failed and the original file permissions could not be restored. Use the manual guide");
                throw;
            }
        }

        void AssertNoCertificateDependents(const Locations& locations, PCCERT_CONTEXT certificate) {
            for (const auto& entry : Fs::directory_iterator(locations.inf)) {
                if (!PublishedName(entry.path().filename().native()))
                    continue;
                const auto directory = StoreInf(locations, entry.path()).parent_path();
                bool inspected = false;
                for (const auto& file : Fs::directory_iterator(directory)) {
                    if (!Equal(file.path().extension().native(), L".cat"))
                        continue;
                    inspected = true;
                    if (CatalogUsesCertificate(ReadOrdinaryFile(file.path()), certificate))
                        throw std::runtime_error(
                            "An installed or staged driver package still uses the tester certificate.");
                }
                if (!inspected)
                    throw std::runtime_error(
                        "A staged driver catalog could not be identified. Tester trust was retained.");
            }
        }
    }

    bool State::HasDriverComponents() const {
        return !publishedInf.empty() || driver || monitor || !queues.empty() || !files.empty();
    }

    bool State::HasPendingJobs() const {
        return std::ranges::any_of(queues, [](const auto& queue) { return queue.jobs != 0; });
    }

    State Inspect(const Fs::path& package) {
        return Inspect(Locations{}, ReadReference(package));
    }

    State Remove(const Fs::path& package, const State& confirmed, const Progress& progress) {
        const Locations locations;
        const auto reference = ReadReference(package);
        auto current = Inspect(locations, reference);
        if (current != confirmed || current.HasPendingJobs())
            throw std::runtime_error("The installed components changed. Review the removal again.");

        progress(L"Removing the confirmed printer queues...");
        for (const auto& queue : current.queues)
            RemoveQueue(queue);
        current = Inspect(locations, reference);
        if (!current.queues.empty() || current.publishedInf != confirmed.publishedInf)
            throw std::runtime_error("A printer queue or package changed. Review removal again.");
        if (current.driver) {
            progress(L"Removing the printer driver registration...");
            const Privilege loadDriver(SE_LOAD_DRIVER_NAME);
            Require(DeletePrinterDriverExW(nullptr, const_cast<wchar_t*>(ENVIRONMENT), const_cast<wchar_t*>(MODEL),
                                           DPD_DELETE_UNUSED_FILES, 0),
                    "Could not unregister the printer driver");
        }
        current = Inspect(locations, reference);
        if (!current.queues.empty() || current.driver || current.publishedInf != confirmed.publishedInf)
            throw std::runtime_error("A driver dependency remains or the Driver Store package changed.");
        if (!current.publishedInf.empty()) {
            progress(L"Removing the Driver Store package...");
            RemovePackage(locations, current.publishedInf);
        }
        current = Inspect(locations, reference);
        if (!current.queues.empty() || current.driver || !current.publishedInf.empty())
            throw std::runtime_error("A printer driver dependency remains.");
        if (current.monitor) {
            progress(L"Unregistering the Benteveo firmware monitor...");
            const Privilege loadDriver(SE_LOAD_DRIVER_NAME);
            Require(DeleteMonitorW(nullptr, const_cast<wchar_t*>(ENVIRONMENT), const_cast<wchar_t*>(MONITOR)),
                    "Could not unregister the Benteveo firmware monitor");
        }
        progress(L"Removing verified System32 files...");
        for (const auto& path : current.files) {
            const auto remaining = Inspect(locations, reference);
            if (!remaining.queues.empty() || remaining.driver || !remaining.publishedInf.empty() || remaining.monitor)
                throw std::runtime_error("A printer component still references the files.");
            if (std::ranges::find(remaining.files, path) != remaining.files.end())
                RemoveFile(path, Equal(path.filename().native(), MONITOR_DLL) ? reference.hashes[1] : FIRMWARE_SHA256);
        }
        current = Inspect(locations, reference);
        if (current.HasDriverComponents())
            throw std::runtime_error("Some Benteveo components remain. Review the removal again.");
        return current;
    }

    State RemoveTrust(const Fs::path& package) {
        const Locations locations;
        const auto reference = ReadReference(package);
        auto current = Inspect(locations, reference);
        if (current.HasDriverComponents())
            throw std::runtime_error("Complete driver cleanup before removing tester trust.");
        if (!current.testerTrust)
            return current;
        AssertNoCertificateDependents(locations, reference.certificate.get());
        for (const auto* name : STORES) {
            const Store store(name, false);
            if (auto found = store.Find(reference.certificate.get())) {
                Require(CertDeleteCertificateFromStore(found.release()),
                        "Could not remove the exact tester certificate");
                if (store.Find(reference.certificate.get()))
                    throw std::runtime_error(
                        "Tester trust remains in a local computer store. Check whether it is managed by policy.");
            }
        }
        current = Inspect(locations, reference);
        if (current.testerTrust)
            throw std::runtime_error("Tester trust remains in a local computer store.");
        return current;
    }
}
