// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <winspool.h>
#include <setupapi.h>
#include <wintrust.h>
#include <softpub.h>
#include <bcrypt.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <shellapi.h>
#include <commctrl.h>
#include <aclapi.h>
#include "lj1005/usb.hpp"
#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <thread>
#include <iostream>
#include <utility>

#pragma comment(                                                                                                       \
    linker,                                                                                                            \
    "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")

namespace {
    namespace fs = std::filesystem;
    constexpr auto MODEL = L"HP LaserJet 1005 Series (Open ZJS)";
    constexpr auto QUEUE = L"HP LaserJet 1005 (Open ZJS)";
    constexpr auto MONITOR = L"HP LaserJet 1005 Firmware Monitor (development)";
    constexpr auto STATE = L"SOFTWARE\\OpenZJS\\LaserJet1005";
    constexpr auto OWNER = L"{851B5A16-66F6-4F48-8D74-0CFBD5FA9947}";
    constexpr auto ARP = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\OpenZJS-LaserJet1005";
    constexpr auto UPDATE = L"SOFTWARE\\OpenZJS\\LaserJet1005Update";
    constexpr auto FW_HASH = L"638b9ad4a2cf6888208926ef7c7de073da8ba4cded25d31000a585c4526a9ca1";
    constexpr std::array PAYLOAD = {L"lj1005.inf", L"lj1005.cat",       L"lj1005.gpd",
                                    L"lj1005.ini", L"lj1005render.dll", L"lj1005monitor.dll"};

    struct Failure {
        std::wstring message;
    };

    [[noreturn]] void Fail(const std::wstring& message, DWORD error = 0) {
        throw Failure{message + (error ? L"\nWindows error: " + std::to_wstring(error) : L"")};
    }

    void Check(BOOL result, const wchar_t* message) {
        if (!result)
            Fail(message, GetLastError());
    }

    struct Key {
        HKEY value = nullptr;

        ~Key() {
            if (value)
                RegCloseKey(value);
        }

        Key(const Key&) = delete;

        explicit Key(const wchar_t* name, bool create = false) {
            const auto error = create
                                   ? RegCreateKeyExW(HKEY_LOCAL_MACHINE, name, 0, nullptr, 0,
                                                     KEY_READ | KEY_WRITE | KEY_WOW64_64KEY, nullptr, &value, nullptr)
                                   : RegOpenKeyExW(HKEY_LOCAL_MACHINE, name, 0, KEY_READ | KEY_WOW64_64KEY, &value);
            if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
                Fail(L"Cannot access installation settings.", error);
        }
    };

    std::wstring ReadSetting(const wchar_t* keyName, const wchar_t* name) {
        Key key(keyName);
        if (!key.value)
            return {};
        std::array<wchar_t, 32768> buffer{};
        DWORD bytes = static_cast<DWORD>(sizeof(buffer));
        const auto error = RegGetValueW(key.value, nullptr, name, RRF_RT_REG_SZ, nullptr, buffer.data(), &bytes);
        if (error == ERROR_FILE_NOT_FOUND)
            return {};
        if (error != ERROR_SUCCESS)
            Fail(L"Cannot read installation settings.", error);
        return buffer.data();
    }

    void WriteSetting(const wchar_t* keyName, const wchar_t* name, const std::wstring& value) {
        Key key(keyName, true);
        const auto error = RegSetValueExW(key.value, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()),
                                          static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
        if (error != ERROR_SUCCESS)
            Fail(L"Cannot save installation settings.", error);
        RegFlushKey(key.value);
    }

    bool Flag(const wchar_t* name) {
        return ReadSetting(STATE, name) == L"1";
    }

    void Mark(const wchar_t* name, bool value = true) {
        WriteSetting(STATE, name, value ? L"1" : L"0");
    }

    fs::path ModulePath() {
        std::array<wchar_t, 32768> path{};
        const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
        if (!size || size >= path.size())
            Fail(L"Cannot locate Setup.");
        return path.data();
    }

    fs::path SystemPath(const wchar_t* file) {
        std::array<wchar_t, MAX_PATH> path{};
        Check(GetSystemDirectoryW(path.data(), static_cast<UINT>(path.size())), L"Cannot locate Windows.");
        return fs::path(path.data()) / file;
    }

    fs::path InstallRoot() {
        PWSTR path = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_ProgramFiles, 0, nullptr, &path)))
            Fail(L"Cannot locate Program Files.");
        fs::path result = fs::path(path) / L"OpenZJS LaserJet 1005";
        CoTaskMemFree(path);
        return result;
    }

    void RejectLinks(const fs::path& path) {
        for (auto current = fs::absolute(path); !current.empty(); current = current.parent_path()) {
            const auto attributes = GetFileAttributesW(current.c_str());
            if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
                Fail(L"An installation destination is a redirected path. No files will be replaced.");
            if (current == current.parent_path())
                break;
        }
    }

    std::vector<BYTE> ReadBytes(const fs::path& path, std::size_t maximum = 32 * 1024 * 1024) {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        const auto length = file ? file.tellg() : std::streampos(-1);
        if (length < 0 || static_cast<std::uint64_t>(length) > maximum)
            Fail(L"Cannot read the selected file, or it is too large: " + path.filename().wstring());
        std::vector<BYTE> bytes(static_cast<std::size_t>(length));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!file)
            Fail(L"Could not read the complete file.");
        return bytes;
    }

    std::wstring Hash(const std::vector<BYTE>& bytes) {
        std::array<BYTE, 32> digest{};
        if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE, nullptr, 0, const_cast<PUCHAR>(bytes.data()),
                       static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())) < 0)
            Fail(L"Could not verify file integrity.");
        std::wstring result;
        for (auto value : digest) {
            result += L"0123456789abcdef"[value >> 4];
            result += L"0123456789abcdef"[value & 15];
        }
        return result;
    }

    void WriteNew(const fs::path& path, const std::vector<BYTE>& bytes) {
        // Create a new file with the protected destination's inherited ACL.
        // CopyFile can carry a user-writable source ACL into Program Files.
        const auto file =
            CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
            Fail(L"Cannot create installation file: " + path.filename().wstring(), GetLastError());
        DWORD written = 0;
        const bool ok = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
                        written == bytes.size() && FlushFileBuffers(file);
        const auto error = GetLastError();
        CloseHandle(file);
        if (!ok)
            Fail(L"Cannot save installation file.", error);
    }

    std::vector<BYTE> Firmware(const fs::path& selected) {
        if (selected.empty() || !fs::is_regular_file(selected))
            Fail(L"Select sihp1005.img from the extracted original HP driver package.");
        const auto bytes = ReadBytes(selected, 116892);
        if (bytes.size() != 116892 || Hash(bytes) != FW_HASH)
            Fail(L"This firmware does not match the original HP LaserJet 1005. Select the original sihp1005.img file.");
        return bytes;
    }

    bool IsAdmin() {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        PSID sid = nullptr;
        BOOL member = FALSE;
        if (AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0,
                                     0, &sid)) {
            CheckTokenMembership(nullptr, sid, &member);
            FreeSid(sid);
        }
        return member != FALSE;
    }

    struct InstallationLock {
        HANDLE handle = nullptr;

        InstallationLock() {
            handle = CreateMutexW(nullptr, FALSE, L"Global\\OpenZJS.LaserJet1005.Setup");
            if (!handle)
                Fail(L"Cannot lock the printer installation.", GetLastError());
            const auto wait = WaitForSingleObject(handle, 0);
            if (wait != WAIT_OBJECT_0 && wait != WAIT_ABANDONED) {
                CloseHandle(handle);
                handle = nullptr;
                Fail(L"Another Setup operation is running. Wait for it to finish.");
            }
        }

        ~InstallationLock() {
            if (handle) {
                ReleaseMutex(handle);
                CloseHandle(handle);
            }
        }

        InstallationLock(const InstallationLock&) = delete;
    };

    void Preflight(bool installing = true) {
        if (!IsAdmin())
            Fail(L"Administrator permission is required to change the printer installation.");
        SYSTEM_INFO info{};
        GetNativeSystemInfo(&info);
        if (info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64)
            Fail(L"This installer requires Windows x64. ARM64 is not supported.");
        const auto build = ReadSetting(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion", L"CurrentBuildNumber");
        if (build.empty() || std::stoul(build) < 26100)
            Fail(L"This preview requires Windows 11 24H2 or later on x64.");
        Key wpp(L"SOFTWARE\\Policies\\Microsoft\\Windows NT\\Printers\\WPP");
        if (installing && wpp.value) {
            for (const auto name : {L"WindowsProtectedPrintMode", L"WindowsProtectedPrintGroupPolicyState"}) {
                DWORD value = 0, size = sizeof(value);
                const auto error = RegGetValueW(wpp.value, nullptr, name, RRF_RT_REG_DWORD, nullptr, &value, &size);
                if (error != ERROR_SUCCESS && error != ERROR_FILE_NOT_FOUND)
                    Fail(L"Cannot determine Windows Protected Print Mode settings.", error);
                if (value)
                    Fail(
                        L"Windows Protected Print Mode blocks this driver. Review Printers & scanners settings or contact your administrator.");
            }
        }
        const auto manager = OpenSCManagerW(nullptr, nullptr, SC_MANAGER_CONNECT);
        if (!manager)
            Fail(L"Cannot check the Print Spooler service.", GetLastError());
        const auto service = OpenServiceW(manager, L"Spooler", SERVICE_QUERY_STATUS);
        SERVICE_STATUS status{};
        const bool running =
            service && QueryServiceStatus(service, &status) && status.dwCurrentState == SERVICE_RUNNING;
        if (service)
            CloseServiceHandle(service);
        CloseServiceHandle(manager);
        if (!running)
            Fail(L"Start the Windows Print Spooler service, then run Setup again.");
    }

    void TrustedCatalog(const fs::path& path) {
        WINTRUST_FILE_INFO file{};
        file.cbStruct = sizeof(file);
        file.pcwszFilePath = path.c_str();
        WINTRUST_DATA trust{};
        trust.cbStruct = sizeof(trust);
        trust.dwUIChoice = WTD_UI_NONE;
        trust.fdwRevocationChecks = WTD_REVOKE_WHOLECHAIN;
        trust.dwProvFlags = WTD_REVOCATION_CHECK_CHAIN_EXCLUDE_ROOT;
        trust.dwUnionChoice = WTD_CHOICE_FILE;
        trust.pFile = &file;
        trust.dwStateAction = WTD_STATEACTION_VERIFY;
        GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
        const auto result = WinVerifyTrust(nullptr, &action, &trust);
        trust.dwStateAction = WTD_STATEACTION_CLOSE;
        WinVerifyTrust(nullptr, &action, &trust);
        if (result != ERROR_SUCCESS)
            Fail(
                L"Windows does not trust this driver package. Obtain a signed release; do not disable signature enforcement.",
                static_cast<DWORD>(result));
    }

    std::wstring UsbPort() {
        const auto devices = lj1005::GetUsbDevices();
        if (devices.size() != 1)
            Fail(L"Connect and turn on exactly one original HP LaserJet 1005 USB printer, then try again.");
        const auto base = L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors\\USB Monitor\\Ports";
        Key ports(base);
        std::wstring found;
        if (ports.value) {
            for (DWORD index = 0;; ++index) {
                std::array<wchar_t, 256> name{};
                DWORD length = static_cast<DWORD>(name.size());
                const auto error =
                    RegEnumKeyExW(ports.value, index, name.data(), &length, nullptr, nullptr, nullptr, nullptr);
                if (error == ERROR_NO_MORE_ITEMS)
                    break;
                if (error != ERROR_SUCCESS)
                    Fail(L"Cannot enumerate printer USB ports.", error);
                const auto device = ReadSetting((std::wstring(base) + L"\\" + name.data()).c_str(), L"Device Id");
                if (_wcsicmp(device.c_str(), devices[0].instance.c_str()) == 0) {
                    if (!found.empty())
                        Fail(L"Several ports refer to this printer. Resolve the duplicate port registration first.");
                    found = name.data();
                }
            }
        }
        if (found.empty())
            Fail(L"Windows has not created a USB printer port yet. Reconnect the printer and try again.");
        return found;
    }

    bool QueueExists() {
        HANDLE printer = nullptr;
        if (OpenPrinterW(const_cast<LPWSTR>(QUEUE), &printer, nullptr)) {
            ClosePrinter(printer);
            return true;
        }
        const auto error = GetLastError();
        if (error != ERROR_INVALID_PRINTER_NAME && error != ERROR_FILE_NOT_FOUND)
            Fail(L"Cannot inspect the existing printer queue.", error);
        return false;
    }

    void CheckQueues() {
        DWORD needed = 0, count = 0;
        EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, nullptr, 0, &needed, &count);
        std::vector<BYTE> bytes(needed);
        if (needed)
            Check(EnumPrintersW(PRINTER_ENUM_LOCAL, nullptr, 2, bytes.data(), needed, &needed, &count),
                  L"Cannot inspect printer queues.");
        const auto* queues = reinterpret_cast<const PRINTER_INFO_2W*>(bytes.data());
        for (DWORD i = 0; i < count; ++i) {
            const auto& queue = queues[i];
            if (_wcsicmp(queue.pDriverName, MODEL) == 0) {
                if (_wcsicmp(queue.pPrinterName, QUEUE) != 0)
                    Fail(L"Another queue uses this driver. Remove or migrate that queue before continuing.");
                if (queue.cJobs)
                    Fail(L"Finish or cancel this printer's pending jobs before changing the installation.");
            }
        }
        needed = count = 0;
        EnumPrinterDriversW(nullptr, const_cast<LPWSTR>(L"Windows x64"), 3, nullptr, 0, &needed, &count);
        bytes.resize(needed);
        if (needed)
            Check(EnumPrinterDriversW(nullptr, const_cast<LPWSTR>(L"Windows x64"), 3, bytes.data(), needed, &needed,
                                      &count),
                  L"Cannot inspect installed printer drivers.");
        const auto* drivers = reinterpret_cast<const DRIVER_INFO_3W*>(bytes.data());
        for (DWORD i = 0; i < count; ++i) {
            if (drivers[i].pMonitorName && _wcsicmp(drivers[i].pMonitorName, MONITOR) == 0 &&
                _wcsicmp(drivers[i].pName, MODEL) != 0)
                Fail(L"Another driver uses this firmware monitor. Its installation will not be changed.");
        }
    }

    HANDLE OpenOwnedQueue() {
        HANDLE printer = nullptr;
        PRINTER_DEFAULTSW defaults{};
        defaults.DesiredAccess = PRINTER_ALL_ACCESS;
        Check(OpenPrinterW(const_cast<LPWSTR>(QUEUE), &printer, &defaults), L"Cannot open the installed queue.");
        std::array<wchar_t, 128> marker{};
        DWORD needed = 0, type = 0;
        const auto error =
            GetPrinterDataExW(printer, L"PrinterDriverData", L"OpenZJSOwner", &type,
                              reinterpret_cast<BYTE*>(marker.data()), static_cast<DWORD>(sizeof(marker)), &needed);
        if (error != ERROR_SUCCESS || type != REG_SZ || needed != (wcslen(OWNER) + 1) * sizeof(wchar_t) ||
            marker[needed / sizeof(wchar_t) - 1] != L'\0' || std::wstring(marker.data()) != OWNER) {
            ClosePrinter(printer);
            Fail(L"The existing queue was not created by this installer. It will not be changed.");
        }
        DWORD size = 0;
        GetPrinterW(printer, 2, nullptr, 0, &size);
        std::vector<BYTE> data(size);
        if (!GetPrinterW(printer, 2, data.data(), size, &size) ||
            _wcsicmp(reinterpret_cast<PRINTER_INFO_2W*>(data.data())->pDriverName, MODEL) != 0) {
            ClosePrinter(printer);
            Fail(L"The queue now uses another driver. It will not be changed.");
        }
        return printer;
    }

    void ConfigureQueue(HANDLE printer, const std::wstring& port) {
        DWORD needed = 0;
        GetPrinterW(printer, 2, nullptr, 0, &needed);
        std::vector<BYTE> bytes(needed);
        Check(GetPrinterW(printer, 2, bytes.data(), needed, &needed), L"Cannot read printer settings.");
        auto* info = reinterpret_cast<PRINTER_INFO_2W*>(bytes.data());
        if (_wcsicmp(info->pDriverName, MODEL) != 0)
            Fail(L"The owned queue now uses another driver. It will not be changed.");
        info->pPortName = const_cast<LPWSTR>(port.c_str());
        info->Attributes |= PRINTER_ATTRIBUTE_ENABLE_BIDI;
        info->Attributes &= ~PRINTER_ATTRIBUTE_WORK_OFFLINE;
        info->pSecurityDescriptor = nullptr;
        Check(SetPrinterW(printer, 2, bytes.data(), 0), L"Cannot configure USB communication.");
        const auto size = DocumentPropertiesW(nullptr, printer, const_cast<LPWSTR>(QUEUE), nullptr, nullptr, 0);
        if (size <= 0)
            Fail(L"Cannot read paper settings.");
        std::vector<BYTE> mode(static_cast<std::size_t>(size));
        auto* devmode = reinterpret_cast<DEVMODEW*>(mode.data());
        if (DocumentPropertiesW(nullptr, printer, const_cast<LPWSTR>(QUEUE), devmode, nullptr, DM_OUT_BUFFER) != IDOK)
            Fail(L"Cannot read paper settings.");
        devmode->dmFields |= DM_PAPERSIZE;
        devmode->dmPaperSize = DMPAPER_A4;
        if (DocumentPropertiesW(nullptr, printer, const_cast<LPWSTR>(QUEUE), devmode, devmode,
                                DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK)
            Fail(L"Cannot validate A4 settings.");
        PRINTER_INFO_8W global{devmode};
        Check(SetPrinterW(printer, 8, reinterpret_cast<BYTE*>(&global), 0), L"Cannot save A4 defaults.");
    }

    void CreateOwnedQueue(const std::wstring& port) {
        PRINTER_INFO_2W info{};
        info.pPrinterName = const_cast<LPWSTR>(QUEUE);
        info.pPortName = const_cast<LPWSTR>(port.c_str());
        info.pDriverName = const_cast<LPWSTR>(MODEL);
        info.pPrintProcessor = const_cast<LPWSTR>(L"winprint");
        info.pDatatype = const_cast<LPWSTR>(L"RAW");
        info.Attributes = PRINTER_ATTRIBUTE_LOCAL | PRINTER_ATTRIBUTE_ENABLE_BIDI;
        const auto printer = AddPrinterW(nullptr, 2, reinterpret_cast<BYTE*>(&info));
        if (!printer)
            Fail(L"Cannot create the printer queue.", GetLastError());
        try {
            const auto error = SetPrinterDataExW(printer, L"PrinterDriverData", L"OpenZJSOwner", REG_SZ,
                                                 reinterpret_cast<BYTE*>(const_cast<LPWSTR>(OWNER)),
                                                 static_cast<DWORD>((wcslen(OWNER) + 1) * sizeof(wchar_t)));
            if (error != ERROR_SUCCESS) {
                DeletePrinter(printer);
                Fail(L"Cannot mark queue ownership.", error);
            }
            ConfigureQueue(printer, port);
        }
        catch (...) {
            ClosePrinter(printer);
            throw;
        }
        ClosePrinter(printer);
    }

    void RegisterUninstaller() {
        WriteSetting(ARP, L"DisplayName", L"Open ZJS - HP LaserJet 1005");
        const auto version = ReadSetting(STATE, L"Version");
        WriteSetting(ARP, L"DisplayVersion", version.empty() ? L"0.1.0" : version);
        WriteSetting(ARP, L"Publisher", L"Open ZJS Project");
        WriteSetting(ARP, L"UninstallString", L"\"" + (InstallRoot() / L"Setup.exe").wstring() + L"\" --remove");
    }

    std::wstring HashName(const wchar_t* name) {
        return std::wstring(L"Hash.") + name;
    }

    void RecordRepairIdentity() {
        for (const auto name : PAYLOAD)
            WriteSetting(STATE, HashName(name).c_str(), Hash(ReadBytes(InstallRoot() / L"driver" / name)));
        WriteSetting(STATE, L"Hash.Setup.exe", Hash(ReadBytes(InstallRoot() / L"Setup.exe")));
        WriteSetting(STATE, L"RepairSchema", L"1");
    }

    fs::path DriverDirectory() {
        DWORD needed = 0;
        GetPrinterDriverDirectoryW(nullptr, const_cast<LPWSTR>(L"Windows x64"), 1, nullptr, 0, &needed);
        std::vector<BYTE> buffer(needed);
        Check(
            GetPrinterDriverDirectoryW(nullptr, const_cast<LPWSTR>(L"Windows x64"), 1, buffer.data(), needed, &needed),
            L"Cannot locate the Windows printer driver directory.");
        // The API returns the environment root (x64); this is a Type 3 driver.
        return fs::path(reinterpret_cast<const wchar_t*>(buffer.data())) / L"3";
    }

    bool CheckRegisteredDriver(const fs::path& inf, const fs::path& directory) {
        DWORD needed = 0, count = 0;
        const auto environment = const_cast<LPWSTR>(L"Windows x64");
        if (!EnumPrinterDriversW(nullptr, environment, 8, nullptr, 0, &needed, &count) &&
            GetLastError() != ERROR_INSUFFICIENT_BUFFER)
            Fail(L"Cannot inspect the installed driver package.", GetLastError());
        if (!needed)
            return false;
        std::vector<BYTE> buffer(needed);
        Check(EnumPrinterDriversW(nullptr, environment, 8, buffer.data(), needed, &needed, &count),
              L"Cannot inspect the installed driver package.");
        const auto* drivers = reinterpret_cast<const DRIVER_INFO_8W*>(buffer.data());
        for (DWORD i = 0; i < count; ++i) {
            const auto& driver = drivers[i];
            if (_wcsicmp(driver.pName, MODEL) != 0)
                continue;
            if (!driver.pszInfPath || _wcsicmp(driver.pszInfPath, inf.c_str()) != 0 || !driver.pMonitorName ||
                _wcsicmp(driver.pMonitorName, MONITOR) != 0 || !driver.pDataFile ||
                _wcsicmp(driver.pDataFile, (directory / L"lj1005.gpd").c_str()) != 0 ||
                !(driver.dwPrinterDriverAttributes & PRINTER_DRIVER_PACKAGE_AWARE))
                Fail(L"The registered driver no longer matches the owned package. It will not be replaced.");
            return true;
        }
        return false;
    }

    bool MatchesFile(const fs::path& file, const std::wstring& expected) {
        RejectLinks(file);
        return fs::is_regular_file(file) && Hash(ReadBytes(file)) == expected;
    }

    void RestoreOwnedFile(const fs::path& file, const std::vector<BYTE>& bytes) {
        RejectLinks(file);
        if (MatchesFile(file, Hash(bytes)))
            return;
        // Replace the directory entry, never write through a possible hard link.
        // A fixed sibling is safe under the protected installation directories;
        // interrupted writes are retried under the installation mutex.
        const auto temporary = fs::path(file.wstring() + L".repair");
        RejectLinks(temporary);
        if (fs::exists(temporary))
            Check(DeleteFileW(temporary.c_str()), L"Cannot clear an interrupted repair file.");
        try {
            WriteNew(temporary, bytes);
            Check(
                MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
                L"Cannot restore an installation file. Close applications using it or restart Windows, then run repair again.");
        }
        catch (...) {
            DeleteFileW(temporary.c_str());
            throw;
        }
    }

    void DeleteRepairTemporary(const fs::path& file) {
        const auto temporary = fs::path(file.wstring() + L".repair");
        RejectLinks(temporary);
        if (fs::exists(temporary))
            Check(DeleteFileW(temporary.c_str()), L"Cannot remove an interrupted repair file.");
    }

    void RepairOwned(const fs::path& source, const std::vector<BYTE>& firmware, const std::wstring& port) {
        const auto status = ReadSetting(STATE, L"Status");
        if (status == L"RebootPending")
            Fail(L"Restart Windows to finish removal, then run Setup again.");
        if (status != L"Complete" && status != L"Repairing" && status != L"NeedsRepair")
            Fail(L"A previous installation was interrupted. Run Remove, then install again.");
        if (ReadSetting(STATE, L"RepairSchema") != L"1")
            Fail(
                L"This older installation has no repair identity. Remove it, restart if requested, then install again.");
        // Keep verified bytes in memory: the supplied package may be user-writable.
        std::array<std::vector<BYTE>, PAYLOAD.size()> payload;
        for (std::size_t i = 0; i < PAYLOAD.size(); ++i) {
            payload[i] = ReadBytes(source / PAYLOAD[i]);
            const auto expected = ReadSetting(STATE, HashName(PAYLOAD[i]).c_str());
            if (expected.empty() || Hash(payload[i]) != expected)
                Fail(
                    L"Repair requires the exact package used to install this build. The selected package differs or is damaged.");
            RejectLinks(InstallRoot() / L"driver" / PAYLOAD[i]);
        }
        const auto setup = ReadBytes(ModulePath());
        if (Hash(setup) != ReadSetting(STATE, L"Hash.Setup.exe"))
            Fail(L"Repair requires the original Setup for this build. Choose Upgrade to install a different build.");
        RejectLinks(InstallRoot() / L"Setup.exe");
        RejectLinks(SystemPath(L"lj1005monitor.dll"));
        const fs::path stored = ReadSetting(STATE, L"InfPath");
        if (stored.empty() || stored.filename() != L"lj1005.inf" || !Flag(L"Driver") || !Flag(L"Monitor") ||
            !Flag(L"MonitorFile") || !Flag(L"Firmware"))
            Fail(L"The installation ownership record is incomplete. Repair cannot safely continue.");
        const auto directory = DriverDirectory();
        CheckRegisteredDriver(stored, directory);
        const auto monitorKey = std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors\\") + MONITOR;
        const auto monitorDll = ReadSetting(monitorKey.c_str(), L"Driver");
        if (!monitorDll.empty() && _wcsicmp(monitorDll.c_str(), L"lj1005monitor.dll") != 0)
            Fail(L"The firmware monitor registration has changed. It will not be replaced.");
        if (QueueExists()) {
            const auto printer = OpenOwnedQueue();
            ClosePrinter(printer);
        }
        for (const auto name : {L"lj1005.gpd", L"lj1005.ini", L"lj1005render.dll"})
            RejectLinks(directory / name);
        // No changes until source identity, ownership, dependencies and paths pass.
        WriteSetting(STATE, L"Status", L"Repairing");
        try {
            fs::create_directories(InstallRoot() / L"driver");
            for (std::size_t i = 0; i < PAYLOAD.size(); ++i)
                RestoreOwnedFile(InstallRoot() / L"driver" / PAYLOAD[i], payload[i]);
            RestoreOwnedFile(InstallRoot() / L"Setup.exe", setup);
            const auto inf = InstallRoot() / L"driver" / L"lj1005.inf";
            TrustedCatalog(InstallRoot() / L"driver" / L"lj1005.cat");
            std::array<wchar_t, 32768> published{}, staged{};
            Check(SetupCopyOEMInfW(inf.c_str(), nullptr, SPOST_PATH, 0, published.data(),
                                   static_cast<DWORD>(published.size()), nullptr, nullptr),
                  L"Windows could not restore the driver package. Repair is incomplete; run Setup again.");
            Check(SetupGetInfDriverStoreLocationW(published.data(), nullptr, nullptr, staged.data(),
                                                  static_cast<DWORD>(staged.size()), nullptr),
                  L"Cannot locate the repair package.");
            if (_wcsicmp(staged.data(), stored.c_str()) != 0)
                Fail(L"Windows staged a different package. Repair stopped to preserve the installed driver.");
            for (const auto name : PAYLOAD)
                if (!MatchesFile(stored.parent_path() / name, ReadSetting(STATE, HashName(name).c_str())))
                    Fail(
                        L"The Windows driver-store package is damaged. Repair stopped before loading it; Windows package recovery is required.");
            // Windows owns the live driver files. Reinstall through its package API,
            // ignoring timestamps so missing/corrupt same-version files are restored.
            const auto result =
                InstallPrinterDriverFromPackageW(nullptr, staged.data(), MODEL, L"Windows x64", IPDFP_COPY_ALL_FILES);
            if (FAILED(result))
                Fail(
                    L"Windows could not repair the driver. Close printing applications or restart Windows, then run repair again.",
                    static_cast<DWORD>(result));
            if (!CheckRegisteredDriver(stored, directory))
                Fail(L"Windows did not restore the printer driver registration.");
            for (const auto name : {L"lj1005.gpd", L"lj1005.ini", L"lj1005render.dll"})
                if (!MatchesFile(directory / name, ReadSetting(STATE, HashName(name).c_str())))
                    Fail(L"A driver file is still damaged or in use. Restart Windows, then run repair again.");
            if (!MatchesFile(SystemPath(L"lj1005monitor.dll"), ReadSetting(STATE, L"MonitorHash")) ||
                _wcsicmp(ReadSetting(monitorKey.c_str(), L"Driver").c_str(), L"lj1005monitor.dll") != 0)
                Fail(L"The firmware monitor was not restored. Restart Windows, then run repair again.");
            RestoreOwnedFile(SystemPath(L"lj1005-firmware.img"), firmware);
            if (QueueExists()) {
                const auto printer = OpenOwnedQueue();
                try {
                    ConfigureQueue(printer, port);
                }
                catch (...) {
                    ClosePrinter(printer);
                    throw;
                }
                ClosePrinter(printer);
            }
            else {
                CreateOwnedQueue(port);
            }
            RegisterUninstaller();
            WriteSetting(STATE, L"Status", L"Complete");
        }
        catch (...) {
            WriteSetting(STATE, L"Status", L"NeedsRepair");
            throw;
        }
    }

    void AllowOwnedMonitorDeletion(const fs::path& path) {
        // The spooler installs the monitor with a SYSTEM-only write ACL.
        // Call only after verifying ownership, hash and absence of dependents.
        HANDLE token = nullptr;
        Check(OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token),
              L"Cannot prepare removal of the owned monitor.");
        TOKEN_PRIVILEGES requested{}, previous{};
        requested.PrivilegeCount = 1;
        DWORD previousSize = sizeof(previous);
        if (!LookupPrivilegeValueW(nullptr, SE_TAKE_OWNERSHIP_NAME, &requested.Privileges[0].Luid)) {
            const auto error = GetLastError();
            CloseHandle(token);
            Fail(L"Cannot prepare monitor ownership cleanup.", error);
        }
        requested.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        const bool adjusted =
            AdjustTokenPrivileges(token, FALSE, &requested, sizeof(previous), &previous, &previousSize);
        const auto privilegeError = GetLastError();
        if (!adjusted || privilegeError != ERROR_SUCCESS) {
            CloseHandle(token);
            Fail(L"Cannot obtain permission to remove the owned monitor.", privilegeError);
        }
        std::array<BYTE, SECURITY_MAX_SID_SIZE> admin{};
        DWORD adminSize = static_cast<DWORD>(admin.size());
        DWORD error = ERROR_SUCCESS;
        if (!CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin.data(), &adminSize))
            error = GetLastError();
        else
            error = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
                                          admin.data(), nullptr, nullptr, nullptr);
        AdjustTokenPrivileges(token, FALSE, &previous, 0, nullptr, nullptr);
        CloseHandle(token);
        if (error != ERROR_SUCCESS)
            Fail(L"Cannot take ownership of the verified monitor file for removal.", error);

        PACL current = nullptr, updated = nullptr;
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        error = GetNamedSecurityInfoW(path.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr,
                                      &current, nullptr, &descriptor);
        if (error != ERROR_SUCCESS)
            Fail(L"Cannot read the monitor's removal permissions.", error);
        EXPLICIT_ACCESSW access{};
        access.grfAccessPermissions = DELETE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        access.Trustee.ptstrName = reinterpret_cast<LPWSTR>(admin.data());
        error = SetEntriesInAclW(1, &access, current, &updated);
        if (error == ERROR_SUCCESS)
            error = SetNamedSecurityInfoW(const_cast<LPWSTR>(path.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION,
                                          nullptr, nullptr, updated, nullptr);
        LocalFree(updated);
        LocalFree(descriptor);
        if (error != ERROR_SUCCESS)
            Fail(L"Cannot grant permission to delete the owned monitor.", error);
    }

    void DiscardUpdate();
    bool UpdateInProgress();

    void RemoveOwned(bool updating = false) {
        Preflight(false);
        if (!updating && UpdateInProgress())
            Fail(L"An upgrade or rollback is unfinished. Run Upgrade to recover, or choose Roll back first.");
        if (ReadSetting(STATE, L"Owner") != OWNER)
            Fail(L"No installation owned by this Setup was found.");
        RejectLinks(InstallRoot());
        CheckQueues();
        WriteSetting(STATE, L"Status", L"Removing");
        bool pending = false;
        if (QueueExists()) {
            const auto printer = OpenOwnedQueue();
            const auto deleted = DeletePrinter(printer);
            const auto error = GetLastError();
            ClosePrinter(printer);
            if (!deleted)
                Fail(L"Cannot remove the printer queue.", error);
        }
        if (Flag(L"Driver")) {
            if (!DeletePrinterDriverExW(nullptr, const_cast<LPWSTR>(L"Windows x64"), const_cast<LPWSTR>(MODEL),
                                        DPD_DELETE_UNUSED_FILES, 0) &&
                GetLastError() != ERROR_UNKNOWN_PRINTER_DRIVER)
                Fail(L"Windows could not remove the driver. Close applications using it and try again.",
                     GetLastError());
            Mark(L"Driver", false);
        }
        if (Flag(L"Monitor")) {
            if (!DeleteMonitorW(nullptr, const_cast<LPWSTR>(L"Windows x64"), const_cast<LPWSTR>(MONITOR)) &&
                GetLastError() != ERROR_UNKNOWN_PRINT_MONITOR)
                Fail(
                    updating
                        ? L"Windows could not remove the firmware monitor. Restart Windows, then run this Setup again and choose Upgrade or Roll back to finish recovery."
                        : L"Windows could not remove the firmware monitor. Try removal again after restarting Windows.",
                    GetLastError());
            Mark(L"Monitor", false);
        }
        const auto inf = ReadSetting(STATE, L"InfPath");
        if (!inf.empty()) {
            if (fs::path(inf).filename() != L"lj1005.inf")
                Fail(L"The installation record refers to an unexpected driver package.");
            const auto result = DeletePrinterDriverPackageW(nullptr, inf.c_str(), L"Windows x64");
            if (FAILED(result) && result != HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND))
                Fail(
                    updating
                        ? L"Windows could not remove the stored package. Restart Windows, then run this Setup again and choose Upgrade or Roll back to finish recovery."
                        : L"Windows could not remove the stored package. Try removal again after restarting Windows.",
                    static_cast<DWORD>(result));
            WriteSetting(STATE, L"InfPath", L"");
        }
        if (Flag(L"MonitorFile")) {
            const auto file = SystemPath(L"lj1005monitor.dll");
            RejectLinks(file);
            if (fs::exists(file)) {
                if (Hash(ReadBytes(file)) != ReadSetting(STATE, L"MonitorHash"))
                    Fail(L"The installed monitor file has changed. It will not be deleted.");
                auto deleted = DeleteFileW(file.c_str());
                if (!deleted && GetLastError() == ERROR_ACCESS_DENIED) {
                    AllowOwnedMonitorDeletion(file);
                    deleted = DeleteFileW(file.c_str());
                }
                if (!deleted) {
                    Check(MoveFileExW(file.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT),
                          L"Cannot schedule removal of the monitor file. Restart Windows, then run Remove again.");
                    pending = true;
                }
            }
            if (!pending)
                Mark(L"MonitorFile", false);
        }
        if (Flag(L"Firmware")) {
            const auto firmware = SystemPath(L"lj1005-firmware.img");
            RejectLinks(firmware);
            if (fs::exists(firmware)) {
                if (Hash(ReadBytes(firmware)) != FW_HASH)
                    Fail(L"Installed firmware was changed. It will not be deleted.");
                Check(DeleteFileW(firmware.c_str()), L"Cannot remove firmware.");
            }
            Mark(L"Firmware", false);
        }
        DeleteRepairTemporary(SystemPath(L"lj1005-firmware.img"));
        // Only remove known files, never a recursive directory tree. A running
        // installed Setup is scheduled for deletion at reboot if Windows locks it.
        for (const auto name : PAYLOAD) {
            const auto file = InstallRoot() / L"driver" / name;
            DeleteRepairTemporary(file);
            RejectLinks(file);
            if (fs::exists(file))
                Check(DeleteFileW(file.c_str()), L"Cannot remove cached driver files.");
        }
        RemoveDirectoryW((InstallRoot() / L"driver").c_str());
        const auto setup = InstallRoot() / L"Setup.exe";
        DeleteRepairTemporary(setup);
        if (fs::exists(setup) && !DeleteFileW(setup.c_str())) {
            Check(MoveFileExW(setup.c_str(), nullptr, MOVEFILE_DELAY_UNTIL_REBOOT), L"Cannot schedule Setup removal.");
            pending = true;
        }
        RemoveDirectoryW(InstallRoot().c_str());
        RegDeleteKeyExW(HKEY_LOCAL_MACHINE, ARP, KEY_WOW64_64KEY, 0);
        // Keep ownership until backup cleanup succeeds, so a failure is retryable.
        if (!updating)
            DiscardUpdate();
        if (pending)
            WriteSetting(STATE, L"Status", L"RebootPending");
        else
            RegDeleteKeyExW(HKEY_LOCAL_MACHINE, STATE, KEY_WOW64_64KEY, 0);
    }

    std::wstring PackageVersion(const fs::path& inf) {
        const auto handle = SetupOpenInfFileW(inf.c_str(), nullptr, INF_STYLE_WIN4, nullptr);
        if (handle == INVALID_HANDLE_VALUE)
            Fail(L"Cannot read the driver package version.");
        INFCONTEXT context{};
        std::array<wchar_t, 256> version{};
        const bool ok = SetupFindFirstLineW(handle, L"Version", L"DriverVer", &context) &&
                        SetupGetStringFieldW(&context, 2, version.data(), static_cast<DWORD>(version.size()), nullptr);
        SetupCloseInfFile(handle);
        if (!ok || !version[0])
            Fail(L"The driver package has no version.");
        return version.data();
    }

    void Install(const fs::path& firmwareSource, bool offline, const fs::path& package = {}) {
        Preflight();
        if (package.empty() && UpdateInProgress())
            Fail(L"An upgrade or rollback is unfinished. Run Upgrade to recover, or choose Roll back first.");
        const auto firmware = Firmware(firmwareSource);
        const auto port = offline ? L"FILE:" : UsbPort();
        const auto setup = package.empty() ? ModulePath() : package / L"Setup.exe";
        const auto source = setup.parent_path() / L"driver";
        TrustedCatalog(source / L"lj1005.cat");
        RejectLinks(InstallRoot());
        RejectLinks(SystemPath(L"lj1005-firmware.img"));
        if (ReadSetting(STATE, L"Owner") == OWNER && ReadSetting(STATE, L"Status") == L"RebootPending" &&
            !fs::exists(InstallRoot() / L"Setup.exe") && !fs::exists(SystemPath(L"lj1005monitor.dll")))
            RemoveOwned(!package.empty());
        const bool owned = ReadSetting(STATE, L"Owner") == OWNER;
        CheckQueues();
        if (owned) {
            RepairOwned(source, firmware, port);
            return;
        }
        Key model((std::wstring(
                       L"SYSTEM\\CurrentControlSet\\Control\\Print\\Environments\\Windows x64\\Drivers\\Version-3\\") +
                   MODEL)
                      .c_str());
        Key monitor((std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors\\") + MONITOR).c_str());
        if (model.value || monitor.value || QueueExists() || fs::exists(InstallRoot()) ||
            fs::exists(SystemPath(L"lj1005monitor.dll")) || fs::exists(SystemPath(L"lj1005-firmware.img")))
            Fail(L"An existing or development installation was found. This preview will not adopt or overwrite it.");
        Key oldState(STATE);
        if (oldState.value)
            Fail(L"Unrecognized installation ownership record. No changes were made.");
        WriteSetting(STATE, L"Owner", OWNER);
        WriteSetting(STATE, L"Status", L"Installing");
        try {
            fs::create_directories(InstallRoot() / L"driver");
            WriteNew(InstallRoot() / L"Setup.exe", ReadBytes(setup));
            for (const auto name : PAYLOAD)
                WriteNew(InstallRoot() / L"driver" / name, ReadBytes(source / name));
            TrustedCatalog(InstallRoot() / L"driver" / L"lj1005.cat");
            const auto inf = InstallRoot() / L"driver" / L"lj1005.inf";
            std::array<wchar_t, 32768> published{};
            Check(SetupCopyOEMInfW(inf.c_str(), nullptr, SPOST_PATH, 0, published.data(),
                                   static_cast<DWORD>(published.size()), nullptr, nullptr),
                  L"Windows rejected the driver package.");
            std::array<wchar_t, 32768> stored{};
            Check(SetupGetInfDriverStoreLocationW(published.data(), nullptr, nullptr, stored.data(),
                                                  static_cast<DWORD>(stored.size()), nullptr),
                  L"Cannot locate the staged driver package.");
            WriteSetting(STATE, L"InfPath", stored.data());
            // Record intent before an API that may partially install components.
            Mark(L"Driver");
            Mark(L"Monitor");
            WriteSetting(STATE, L"MonitorHash", Hash(ReadBytes(InstallRoot() / L"driver" / L"lj1005monitor.dll")));
            Mark(L"MonitorFile");
            const auto result = InstallPrinterDriverFromPackageW(nullptr, stored.data(), MODEL, L"Windows x64", 0);
            if (FAILED(result))
                Fail(L"Windows could not register the printer driver.", static_cast<DWORD>(result));
            Mark(L"Firmware");
            WriteNew(SystemPath(L"lj1005-firmware.img"), firmware);
            CreateOwnedQueue(port);
            WriteSetting(STATE, L"Version", PackageVersion(inf));
            RegisterUninstaller();
            RecordRepairIdentity();
            WriteSetting(STATE, L"Status", L"Complete");
        }
        catch (...) {
            try {
                RemoveOwned(!package.empty());
            }
            catch (...) {
                WriteSetting(STATE, L"Status", L"NeedsRemoval");
            }
            throw;
        }
    }

#include "setup_update.hpp"

    std::wstring PickFirmware(HWND owner) {
        IFileOpenDialog* dialog = nullptr;
        if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog))))
            Fail(L"Cannot open the firmware file picker.");
        dialog->SetOptions(FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST);
        const COMDLG_FILTERSPEC filters[] = {{L"HP LaserJet 1005 firmware (sihp1005.img)", L"sihp1005.img"},
                                             {L"Firmware images (*.img)", L"*.img"}};
        dialog->SetFileTypes(2, filters);
        dialog->SetFileName(L"sihp1005.img");
        dialog->SetTitle(L"Select the original HP LaserJet 1005 firmware image");
        std::wstring path;
        if (SUCCEEDED(dialog->Show(owner))) {
            IShellItem* item = nullptr;
            if (SUCCEEDED(dialog->GetResult(&item))) {
                PWSTR name = nullptr;
                if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &name))) {
                    path = name;
                    CoTaskMemFree(name);
                }
                item->Release();
            }
        }
        dialog->Release();
        return path;
    }

    void Elevate(const std::wstring& arguments, HWND owner = nullptr) {
        const auto path = ModulePath();
        SHELLEXECUTEINFOW execute{};
        execute.cbSize = sizeof(execute);
        execute.fMask = SEE_MASK_NOASYNC;
        execute.hwnd = owner;
        execute.lpVerb = L"runas";
        execute.lpFile = path.c_str();
        execute.lpParameters = arguments.c_str();
        execute.nShow = SW_SHOWNORMAL;
        Check(ShellExecuteExW(&execute), L"Administrator permission was not granted.");
    }

    HRESULT CALLBACK SetupDialogCallback(HWND window, UINT notification, WPARAM button, LPARAM, LONG_PTR data) {
        if (notification == TDN_CREATED)
            SetForegroundWindow(window);
        if (notification != TDN_BUTTON_CLICKED)
            return S_OK;

        // Keep the main dialog alive while the picker and elevation UI use it
        // as their owner. No exception may cross the Windows callback boundary.
        auto& result = *reinterpret_cast<int*>(data);
        try {
            if (button == 102 && UpdateInProgress() && ReadSetting(UPDATE, L"Phase") != L"Preparing")
                Elevate(L"--upgrade", window);
            else if (button == 100 || button == 102) {
                const auto file = PickFirmware(window);
                if (!file.empty()) {
                    Firmware(file);
                    Elevate(std::wstring(button == 102 ? L"--upgrade" : L"--install") + L" --firmware \"" + file +
                                L"\"",
                            window);
                }
            }
            else if (button == 101)
                Elevate(L"--remove", window);
            else if (button == 103)
                Elevate(L"--rollback", window);
        }
        catch (const Failure& error) {
            result = 1;
            MessageBoxW(window, error.message.c_str(), L"HP LaserJet 1005 Setup",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
            OutputDebugStringW(error.message.c_str());
        }
        catch (...) {
            result = 1;
            MessageBoxW(
                window,
                L"Setup could not complete the operation. Check the selected files and Windows printer settings.",
                L"HP LaserJet 1005 Setup", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        }
        return S_OK;
    }
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int count = 0;
    const auto arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    bool quiet = false, remove = false, install = false, upgrade = false, rollback = false, offline = false;
    fs::path firmware;
    for (int i = 1; i < count; ++i) {
        const std::wstring arg = arguments[i];
        if (arg == L"--quiet")
            quiet = true;
        else if (arg == L"--remove")
            remove = true;
        else if (arg == L"--install")
            install = true;
        else if (arg == L"--upgrade")
            upgrade = true;
        else if (arg == L"--rollback")
            rollback = true;
        else if (arg == L"--offline")
            offline = true;
        else if (arg == L"--firmware" && i + 1 < count)
            firmware = arguments[++i];
        else {
            LocalFree(arguments);
            CoUninitialize();
            return 2;
        }
    }
    LocalFree(arguments);
    int result = 0;
    try {
        if (!install && !remove && !upgrade && !rollback) {
            const TASKDIALOG_BUTTON buttons[] = {
                {100, L"Install or repair"}, {102, L"Upgrade"}, {103, L"Roll back"}, {101, L"Remove"}};
            TASKDIALOGCONFIG dialog{};
            dialog.cbSize = sizeof(dialog);
            dialog.dwCommonButtons = TDCBF_CANCEL_BUTTON;
            dialog.pszWindowTitle = L"HP LaserJet 1005 Setup";
            dialog.pszMainInstruction = L"Set up your original HP LaserJet 1005";
            dialog.pszContent =
                L"Connect the printer by USB and turn it on. Next, select the sihp1005.img firmware image from your original HP driver package.\n\nThis preview requires a Windows-trusted driver package. P1005 and M1005 printers are not supported.";
            dialog.cButtons = static_cast<UINT>(std::size(buttons));
            dialog.pButtons = buttons;
            dialog.pfCallback = SetupDialogCallback;
            dialog.lpCallbackData = reinterpret_cast<LONG_PTR>(&result);
            if (FAILED(TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr)))
                Fail(L"Cannot open Setup.");
        }
        else {
            if (static_cast<int>(install) + static_cast<int>(remove) + static_cast<int>(upgrade) +
                    static_cast<int>(rollback) !=
                1)
                Fail(L"Choose one operation: installation, upgrade, rollback or removal.");
            if (remove && !quiet &&
                MessageBoxW(nullptr, L"Remove the Open ZJS printer, driver and imported firmware?",
                            L"Remove HP LaserJet 1005", MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND) != IDOK) {
                CoUninitialize();
                return 0;
            }
            if ((upgrade || rollback) && !quiet &&
                MessageBoxW(
                    nullptr,
                    rollback
                        ? L"Restore the previous driver build and its saved printer settings?"
                        : L"Upgrade this printer? Setup saves the previous build and settings for rollback. A restart may be required.",
                    L"HP LaserJet 1005 Setup", MB_OKCANCEL | MB_ICONQUESTION | MB_SETFOREGROUND) != IDOK) {
                CoUninitialize();
                return 0;
            }
            if (!IsAdmin() && !quiet) {
                Elevate(remove     ? L"--remove"
                        : rollback ? L"--rollback"
                                   : std::wstring(upgrade ? L"--upgrade" : L"--install") + L" --firmware \"" +
                                         firmware.wstring() + L"\"" + (offline ? L" --offline" : L""));
            }
            else {
                InstallationLock lock;
                bool restart = false;
                if (upgrade)
                    restart = UpgradeOwned(firmware, offline);
                else if (rollback)
                    restart = RollbackUpdate();
                else if (remove)
                    RemoveOwned();
                else
                    Install(firmware, offline);
                restart = restart || ReadSetting(STATE, L"Status") == L"RebootPending";
                if (restart)
                    result = ERROR_SUCCESS_REBOOT_REQUIRED;
                if (!quiet)
                    MessageBoxW(
                        nullptr,
                        (upgrade || rollback)
                            ? (restart
                                   ? L"Restart Windows, then run this Setup again and choose Upgrade or Roll back to finish recovery."
                                   : L"The update or recovery operation is complete. Your saved printer settings were restored.")
                        : remove
                            ? (restart
                                   ? L"The printer was removed. Restart Windows to finish removing files that were in use."
                                   : L"The printer installation was removed.")
                            : L"Setup is complete. Select HP LaserJet 1005 (Open ZJS) in your application's Print dialog.",
                        L"HP LaserJet 1005 Setup", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            }
        }
    }
    catch (const Failure& error) {
        result = 1;
        if (!quiet)
            MessageBoxW(nullptr, error.message.c_str(), L"HP LaserJet 1005 Setup",
                        MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        OutputDebugStringW(error.message.c_str());
        std::wcerr << error.message << L'\n';
    }
    catch (const std::exception& error) {
        result = 1;
        std::cerr << error.what() << '\n';
        if (!quiet)
            MessageBoxW(
                nullptr,
                L"Setup could not complete the operation. Check the selected files and Windows printer settings.",
                L"HP LaserJet 1005 Setup", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
    CoUninitialize();
    return result;
}
