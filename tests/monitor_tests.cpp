// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <winioctl.h>
#include <usbprint.h>
#include <ntddpar.h>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    void Require(bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    }

    struct FakePort {
        std::vector<unsigned char> bytes;
        std::string identity = "MFG:Hewlett-Packard;MDL:hp LaserJet 1005 series;FWVER:20021030;";
        unsigned starts = 0, ends = 0, closes = 0, queries = 0;
        DWORD writeLimit = 3; // Force partial writes in the lower transport.
        bool writeFails = false, queryFails = false, echoDelivered = false;
    };

    BOOL WINAPI Open(HANDLE instance, LPWSTR, PHANDLE out) {
        *out = instance;
        return TRUE;
    }

    BOOL WINAPI Start(HANDLE h, LPWSTR, DWORD, DWORD, LPBYTE) {
        ++static_cast<FakePort*>(h)->starts;
        return TRUE;
    }

    BOOL WINAPI End(HANDLE h) {
        ++static_cast<FakePort*>(h)->ends;
        return TRUE;
    }

    BOOL WINAPI Close(HANDLE h) {
        ++static_cast<FakePort*>(h)->closes;
        return TRUE;
    }

    BOOL WINAPI Write(HANDLE h, LPBYTE data, DWORD size, LPDWORD written) {
        auto& p = *static_cast<FakePort*>(h);
        if (p.writeFails) {
            *written = 0;
            SetLastError(ERROR_WRITE_FAULT);
            return FALSE;
        }
        *written = std::min(p.writeLimit, size);
        p.bytes.insert(p.bytes.end(), data, data + *written);
        const std::string output(p.bytes.begin(), p.bytes.end());
        if (output.starts_with("\x1b") && output.find("JZJZ") != std::string::npos && !p.echoDelivered) {
            SetLastError(ERROR_INVALID_DATA);
            return FALSE;
        }
        return TRUE;
    }

    BOOL WINAPI Read(HANDLE h, LPBYTE data, DWORD size, LPDWORD received) {
        auto& p = *static_cast<FakePort*>(h);
        const std::string output(p.bytes.begin(), p.bytes.end());
        const auto start = output.find("@PJL JOB NAME=\"");
        if (start == std::string::npos)
            return FALSE;
        const auto tokenStart = start + 15, tokenEnd = output.find('"', tokenStart);
        if (tokenEnd == std::string::npos)
            return FALSE;
        const auto token = output.substr(tokenStart, tokenEnd - tokenStart);
        const bool setup = output.find("@PJL EOJ NAME=") == std::string::npos;
        const auto response = setup ? "@PJL ECHO READY-" + token + "\r\n\f"
                                    : "@PJL USTATUS JOB\r\nEND\r\nNAME=\"" + token + "\"\r\nPAGES=1\r\n\f";
        if (size < response.size())
            return FALSE;
        if (setup)
            p.echoDelivered = true;
        std::memcpy(data, response.data(), response.size());
        *received = static_cast<DWORD>(response.size());
        return TRUE;
    }

    BOOL WINAPI Identify(HANDLE h, DWORD control, LPWSTR, LPWSTR, DWORD, LPWSTR output, DWORD size, LPDWORD returned) {
        auto& p = *static_cast<FakePort*>(h);
        ++p.queries;
        if (p.queryFails) {
            SetLastError(ERROR_NOT_READY);
            return FALSE;
        }
        const std::size_t prefix = control == IOCTL_USBPRINT_GET_1284_ID ? 2 : 0;
        if (control != IOCTL_USBPRINT_GET_1284_ID && control != IOCTL_PAR_QUERY_DEVICE_ID)
            return FALSE;
        *returned = static_cast<DWORD>(prefix + p.identity.size() + 1);
        if (size < *returned) {
            SetLastError(ERROR_INSUFFICIENT_BUFFER);
            return FALSE;
        }
        std::memset(output, 0, *returned);
        std::memcpy(reinterpret_cast<char*>(output) + prefix, p.identity.c_str(), p.identity.size());
        return TRUE;
    }
}

int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2 || argc == 3, "Expected monitor DLL path");
        HMODULE module = LoadLibraryW(argv[1]);
        Require(module != nullptr, "LoadLibrary failed");
        using Init = LPMONITOR2(WINAPI*)(PMONITORINIT, PHANDLE);
        const auto initialize = reinterpret_cast<Init>(GetProcAddress(module, "InitializePrintMonitor2"));
        Require(initialize != nullptr, "Missing InitializePrintMonitor2");
        MONITORINIT info{};
        info.cbSize = sizeof(info);
        info.bLocal = TRUE;
        HANDLE instance = nullptr;
        MONITOR2* monitor = initialize(&info, &instance);
        Require(monitor && instance, "Initialize failed");
        if (argc == 3) {
            Require(std::wstring(argv[2]) == L"--load-only", "Unknown argument");
            monitor->pfnShutdown(instance);
            FreeLibrary(module);
            return 0;
        }
        using AckCount = DWORD(WINAPI*)();
        using Scenario = void(WINAPI*)(DWORD);
        const auto acknowledgements = reinterpret_cast<AckCount>(GetProcAddress(module, "TestAcknowledgements"));
        const auto setScenario = reinterpret_cast<Scenario>(GetProcAddress(module, "TestCompletionScenario"));
        Require(acknowledgements && setScenario, "Expected isolated test fixture backend");
        MONITOR2 lower{};
        lower.cbSize = sizeof(lower);
        lower.pfnOpenPort = Open;
        lower.pfnStartDocPort = Start;
        lower.pfnEndDocPort = End;
        lower.pfnClosePort = Close;
        lower.pfnWritePort = Write;
        lower.pfnReadPort = Read;
        lower.pfnGetPrinterDataFromPort = Identify;
        wchar_t printer[] = L"Test", usb[] = L"USB002", lpt[] = L"LPT1:", file[] = L"FILE:";
        DOC_INFO_1W doc{};
        doc.pDocName = printer;
        doc.pDatatype = const_cast<LPWSTR>(L"RAW");
        const std::vector<unsigned char> job{'J', 'Z', 'J', 'Z', 1, 2, 3, 4, 5};
        for (auto name : {usb, lpt, file}) {
            setScenario(0);
            FakePort fake;
            HANDLE port = nullptr;
            Require(monitor->pfnOpenPortEx(instance, &fake, name, printer, &port, &lower), "OpenPortEx failed");
            Require(monitor->pfnStartDocPort(port, printer, 1, 1, reinterpret_cast<LPBYTE>(&doc)),
                    "StartDocPort failed");
            for (auto byte : job) {
                DWORD count = 0;
                Require(monitor->pfnWritePort(port, &byte, 1, &count) && count == 1, "Split prefix write failed");
            }
            Require(monitor->pfnEndDocPort(port), "EndDocPort failed");
            Require(monitor->pfnClosePort(port), "ClosePort failed");
            if (name == file)
                Require(fake.bytes == job, "Monitor changed file data");
            else {
                const std::string output(fake.bytes.begin(), fake.bytes.end());
                const std::string body(job.begin(), job.end());
                Require(output.starts_with("\x1b%-12345X@PJL\r\n@PJL USTATUS JOB=ON\r\n"), "Missing PJL status enable");
                const auto bodyOffset = output.find(body);
                Require(bodyOffset != std::string::npos &&
                            output.find("@PJL EOJ NAME=", bodyOffset + body.size()) != std::string::npos,
                        "Wrapped job body or EOJ missing");
            }
            Require(acknowledgements() == (name == file ? 0u : 1u), "Incorrect completion acknowledgement count");
            Require(fake.starts == 1 && fake.ends == 1 && fake.closes == 1, "Unbalanced port lifecycle");
            Require(fake.queries == (name == file ? 0u : 1u), "Unexpected identity query count");
        }
        for (unsigned scenario = 0; scenario < 5; ++scenario) {
            setScenario(0);
            FakePort fake;
            HANDLE port = nullptr;
            if (scenario == 0)
                fake.identity = "MFG:EPSON;MDL:Other;";
            if (scenario == 1)
                fake.queryFails = true;
            Require(monitor->pfnOpenPortEx(instance, &fake, usb, printer, &port, &lower), "Open error fixture");
            const auto started = monitor->pfnStartDocPort(port, printer, 1, 1, reinterpret_cast<LPBYTE>(&doc));
            if (scenario <= 1) {
                Require(!started, "Unknown or unavailable printer accepted");
            }
            else {
                Require(started, "Known printer rejected");
                DWORD written = 999;
                if (scenario == 2) {
                    unsigned char garbage[]{0, 1, 2, 3};
                    Require(!monitor->pfnWritePort(port, garbage, 4, &written) && written == 0,
                            "Non-ZJS stream accepted");
                }
                else if (scenario == 3) {
                    unsigned char shortHeader[]{'J', 'Z'};
                    Require(monitor->pfnWritePort(port, shortHeader, 2, &written), "Short header buffer failed");
                }
                else {
                    fake.writeFails = true;
                    auto bytes = job;
                    Require(!monitor->pfnWritePort(port, bytes.data(), static_cast<DWORD>(bytes.size()), &written),
                            "Write error swallowed");
                    Require(GetLastError() == ERROR_WRITE_FAULT, "Write error not preserved");
                }
                Require(!monitor->pfnEndDocPort(port), "Broken job reported success");
            }
            Require(fake.bytes.empty(), "Failed job reached port");
            Require(fake.ends == 1, "Failure did not end lower job exactly once");
            Require(acknowledgements() == 0, "Failed job was acknowledged as printed");
            Require(monitor->pfnClosePort(port), "Close failure fixture");
        }
        for (DWORD scenario : {1u, 2u}) {
            setScenario(0);
            FakePort fake;
            HANDLE port = nullptr;
            Require(monitor->pfnOpenPortEx(instance, &fake, usb, printer, &port, &lower),
                    "Open completion failure fixture");
            Require(monitor->pfnStartDocPort(port, printer, 1, 1, reinterpret_cast<LPBYTE>(&doc)),
                    "Start completion failure fixture");
            auto bytes = job;
            DWORD written = 0;
            Require(monitor->pfnWritePort(port, bytes.data(), static_cast<DWORD>(bytes.size()), &written),
                    "Write completion failure fixture");
            setScenario(scenario);
            Require(monitor->pfnEndDocPort(port), "Status failure requested a replay of submitted data");
            Require(acknowledgements() == 0 && fake.ends == 1, "Cancelled or unconfirmed job acknowledged");
            Require(monitor->pfnClosePort(port), "Close completion failure fixture");
        }
        for (DWORD scenario : {1u, 2u}) {
            setScenario(scenario);
            FakePort fake;
            HANDLE port = nullptr;
            Require(monitor->pfnOpenPortEx(instance, &fake, usb, printer, &port, &lower), "Open setup failure fixture");
            Require(monitor->pfnStartDocPort(port, printer, 1, 1, reinterpret_cast<LPBYTE>(&doc)),
                    "Start setup failure fixture");
            auto bytes = job;
            DWORD written = 0;
            Require(!monitor->pfnWritePort(port, bytes.data(), static_cast<DWORD>(bytes.size()), &written),
                    "Setup failure accepted");
            const std::string output(fake.bytes.begin(), fake.bytes.end());
            Require(output.find("JZJZ") == std::string::npos && written == 0,
                    "Raster reached printer before setup acknowledgement");
            Require(!monitor->pfnEndDocPort(port), "Failed setup reported success");
            Require(acknowledgements() == 0 && fake.ends == 1, "Setup failure acknowledged or cleanup unbalanced");
            Require(monitor->pfnClosePort(port), "Close setup failure fixture");
        }
        FakePort fake;
        HANDLE port = nullptr;
        auto incomplete = lower;
        incomplete.pfnWritePort = nullptr;
        Require(!monitor->pfnOpenPortEx(instance, &fake, usb, printer, &port, &incomplete) && !port,
                "Invalid lower monitor accepted");
        monitor->pfnShutdown(instance);
        FreeLibrary(module);
        std::cout
            << "Monitor callbacks passed: USB/LPT/FILE, split headers, partial writes, identity and error cleanup\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
