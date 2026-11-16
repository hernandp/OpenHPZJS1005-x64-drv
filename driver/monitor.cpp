// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include <winioctl.h>
#include <usbprint.h>
#include <ntddpar.h>
#include "lj1005/firmware.hpp"
#include "lj1005/pjl.hpp"
#include "lj1005/usb.hpp"
#include "completion_backend.hpp"
#include "trace.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string_view>

namespace {
    struct Error {
        DWORD code;
    };

    void Fail(DWORD code) {
        throw Error{code};
    }

    template <class F> BOOL Protect(F action) noexcept {
        try {
            action();
            return TRUE;
        }
        catch (const Error& error) {
            SetLastError(error.code);
        }
        catch (const std::bad_alloc&) {
            SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        }
        catch (const std::exception& error) {
            OutputDebugStringA(error.what());
            SetLastError(ERROR_INVALID_DATA);
        }
        catch (...) {
            SetLastError(ERROR_GEN_FAILURE);
        }
        return FALSE;
    }

    void Checked(BOOL result) {
        if (!result)
            Fail(GetLastError());
    }
    enum class Kind {
        file,
        usb,
        parallel
    };

    struct Instance {
        std::shared_ptr<lj1005::TraceRegistration> trace = std::make_shared<lj1005::TraceRegistration>();
    };

    struct Port {
        std::shared_ptr<lj1005::TraceRegistration> trace;
        MONITOR2 lower{};
        HANDLE handle = nullptr;
        Kind kind = Kind::file;
        std::mutex mutex;
        bool active = false, failed = false;
        std::array<std::uint8_t, 4> prefix{};
        std::size_t prefixSize = 0;
        std::wstring name;
        std::string token;
        std::unique_ptr<lj1005::CompletionBackend> completion;
    };

    std::atomic<unsigned long long> jobSequence{0};

    Kind GetPortKind(std::wstring_view name) {
        if (name == L"FILE:")
            return Kind::file;
        const bool usb = name.starts_with(L"USB"), parallel = name.starts_with(L"LPT");
        if (!usb && !parallel)
            Fail(ERROR_UNKNOWN_PORT);
        name.remove_prefix(3);
        if (parallel && name.ends_with(L":"))
            name.remove_suffix(1);
        if (name.empty() || !std::all_of(name.begin(), name.end(), [](wchar_t c) { return c >= L'0' && c <= L'9'; }))
            Fail(ERROR_UNKNOWN_PORT);
        return usb ? Kind::usb : Kind::parallel;
    }

    Port& GetPort(HANDLE handle) {
        if (!handle)
            Fail(ERROR_INVALID_HANDLE);
        return *static_cast<Port*>(handle);
    }

    std::string Identify(Port& p) {
        if (!p.lower.pfnGetPrinterDataFromPort)
            Fail(ERROR_NOT_SUPPORTED);
        std::array<unsigned char, 4096> bytes{};
        DWORD returned = 0;
        Checked(p.lower.pfnGetPrinterDataFromPort(
            p.handle, p.kind == Kind::usb ? IOCTL_USBPRINT_GET_1284_ID : IOCTL_PAR_QUERY_DEVICE_ID, nullptr, nullptr, 0,
            reinterpret_cast<LPWSTR>(bytes.data()), static_cast<DWORD>(bytes.size()), &returned));
        const std::size_t prefix = p.kind == Kind::usb ? 2 : 0;
        if (returned <= prefix || returned > bytes.size())
            Fail(ERROR_INVALID_DATA);
        const auto first = bytes.begin() + prefix, last = std::find(first, bytes.begin() + returned, 0);
        return std::string(first, last);
    }

    lj1005::Bytes LoadFirmware() {
        // Admin-owned deployment location, independent of current directory, PATH, and user profiles.
        wchar_t directory[MAX_PATH]{};
        const auto length = GetSystemDirectoryW(directory, MAX_PATH);
        if (!length || length >= MAX_PATH)
            Fail(ERROR_PATH_NOT_FOUND);
        const auto path = std::filesystem::path(directory) / L"lj1005-firmware.img";
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
            Fail(ERROR_FILE_NOT_FOUND);
        const auto size = file.tellg();
        if (size < 52 || size > 16 * 1024 * 1024)
            Fail(ERROR_INVALID_DATA);
        lj1005::Bytes data(static_cast<std::size_t>(size));
        file.seekg(0);
        file.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!file)
            Fail(ERROR_READ_FAULT);
        return data;
    }

    void WriteAll(Port& p, std::span<const std::uint8_t> data) {
        for (std::size_t position = 0; position < data.size();) {
            const auto size = static_cast<DWORD>(std::min<std::size_t>(65536, data.size() - position));
            DWORD written = 0;
            Checked(p.lower.pfnWritePort(p.handle, const_cast<LPBYTE>(data.data() + position), size, &written));
            if (!written || written > size)
                Fail(ERROR_WRITE_FAULT);
            position += written;
        }
    }

    BOOL WINAPI OpenPortEx(HANDLE instance, HANDLE lowerInstance, LPWSTR name, LPWSTR, PHANDLE out, MONITOR2* lower) {
        if (out)
            *out = nullptr;
        return Protect([&] {
            if (!instance || !name || !out || !lower || lower->cbSize < MONITOR2_SIZE_WIN2K)
                Fail(ERROR_INVALID_PARAMETER);
            auto p = std::make_unique<Port>();
            p->trace = static_cast<Instance*>(instance)->trace;
            std::memcpy(&p->lower, lower, std::min<std::size_t>(lower->cbSize, sizeof(MONITOR2)));
            if (!p->lower.pfnOpenPort || !p->lower.pfnStartDocPort || !p->lower.pfnWritePort ||
                !p->lower.pfnEndDocPort || !p->lower.pfnClosePort)
                Fail(ERROR_INVALID_PRINT_MONITOR);
            p->kind = GetPortKind(name);
            p->name = name;
            if (p->kind != Kind::file && !p->lower.pfnGetPrinterDataFromPort)
                Fail(ERROR_INVALID_PRINT_MONITOR);
            Checked(p->lower.pfnOpenPort(lowerInstance, name, &p->handle));
            *out = p.release();
        });
    }

    BOOL WINAPI StartDocPort(HANDLE handle, LPWSTR printer, DWORD job, DWORD level, LPBYTE info) {
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (p.active)
                Fail(ERROR_BUSY);
            if (level != 1 || !info)
                Fail(ERROR_INVALID_LEVEL);
            Checked(p.lower.pfnStartDocPort(p.handle, printer, job, level, info));
            p.active = true;
            p.failed = false;
            p.prefixSize = 0;
            p.completion.reset();
            p.token.clear();
            try {
                const auto* doc = reinterpret_cast<DOC_INFO_1W*>(info);
                const bool capture = doc->pOutputFile && *doc->pOutputFile;
                if (p.kind != Kind::file && !capture) {
                    p.token = "lj1005-" + std::to_string(job) + "-" + std::to_string(GetTickCount64()) + "-" +
                              std::to_string(++jobSequence);
                    lj1005::TraceState("Start", p.token.c_str());
                    const bool uploaded =
                        lj1005::EnsureFirmware({[&] { return Identify(p); }, LoadFirmware,
                                                [&](auto data) { WriteAll(p, data); }, [] { Sleep(500); }});
                    lj1005::TraceState("FirmwareReady", p.token.c_str(), uploaded ? "uploaded" : "already running");
                    p.completion = lj1005::CreateCompletionBackend(p.name, p.handle, p.lower, printer, job);
                    lj1005::TraceState("CompletionBackendReady", p.token.c_str());
                }
            }
            catch (...) {
                p.failed = true;
                p.active = false;
                // Release the lower port after failed initialization; send no document data.
                p.lower.pfnEndDocPort(p.handle);
                throw;
            }
        });
    }

    BOOL WINAPI WritePort(HANDLE handle, LPBYTE bytes, DWORD size, LPDWORD written) {
        if (written)
            *written = 0;
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (!written || (!bytes && size))
                Fail(ERROR_INVALID_PARAMETER);
            if (!p.active || p.failed)
                Fail(ERROR_INVALID_STATE);
            try {
                std::size_t consumed = 0;
                // The spooler may split even the four-byte magic across writes. Hold it until validated.
                if (p.prefixSize < 4) {
                    while (consumed < size && p.prefixSize < 4)
                        p.prefix[p.prefixSize++] = bytes[consumed++];
                    if (p.prefixSize == 4) {
                        if (p.prefix != std::array<std::uint8_t, 4>{'J', 'Z', 'J', 'Z'})
                            Fail(ERROR_INVALID_DATA);
                        if (p.completion) {
                            const auto header = lj1005::PjlBeginJob(p.token);
                            WriteAll(p, std::span(reinterpret_cast<const std::uint8_t*>(header.data()), header.size()));
                            // Do not race setup with binary data. Require this job's echoed marker first.
                            lj1005::PjlJobStatus setup(p.token);
                            const auto deadline = GetTickCount64() + 30000;
                            lj1005::TraceState("AwaitSetupEcho", p.token.c_str());
                            while (!setup.IsSetupReady() && GetTickCount64() < deadline) {
                                if (p.completion->IsCancelled())
                                    Fail(ERROR_PRINT_CANCELLED);
                                const auto response = p.completion->Read();
                                lj1005::TraceStatus(p.token.c_str(), response);
                                setup.Feed(response);
                                if (!setup.IsSetupReady())
                                    Sleep(100);
                            }
                            if (!setup.IsSetupReady()) {
                                lj1005::TraceState("SetupEchoTimeout", p.token.c_str());
                                Fail(ERROR_TIMEOUT);
                            }
                            lj1005::TraceState("SetupEchoReceived", p.token.c_str());
                        }
                        WriteAll(p, p.prefix);
                    }
                }
                if (consumed < size)
                    WriteAll(p, std::span(bytes + consumed, size - consumed));
                *written = size;
            }
            catch (...) {
                p.failed = true;
                throw;
            }
        });
    }

    BOOL WINAPI EndDocPort(HANDLE handle) {
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (!p.active)
                Fail(ERROR_INVALID_STATE);
            const bool invalid = p.failed || p.prefixSize != 4;
            bool finished = false;
            if (!invalid && p.completion) {
                try {
                    const auto trailer = lj1005::PjlEndJob(p.token);
                    WriteAll(p, std::span(reinterpret_cast<const std::uint8_t*>(trailer.data()), trailer.size()));
                    lj1005::TraceState("AwaitCompletion", p.token.c_str());
                    lj1005::PjlJobStatus status(p.token);
                    const auto deadline = GetTickCount64() + 30ull * 60 * 1000;
                    ULONGLONG reconnectDeadline = 0;
                    while (GetTickCount64() < deadline) {
                        if (p.completion->IsCancelled()) {
                            lj1005::TraceState("QueueCancelled", p.token.c_str());
                            break;
                        }
                        lj1005::Bytes response;
                        try {
                            response = p.completion->Read();
                        }
                        catch (const lj1005::UsbReadUnavailable& error) {
                            if (!reconnectDeadline) {
                                reconnectDeadline = GetTickCount64() + 30000;
                                lj1005::TraceState("CompletionUsbDisconnected", p.token.c_str(), error.what());
                            }
                            if (GetTickCount64() >= reconnectDeadline) {
                                lj1005::TraceState("CompletionUsbReconnectTimeout", p.token.c_str());
                                break;
                            }
                            // Reopen only the original interface on the next read. No writes or job replay.
                            Sleep(100);
                            continue;
                        }
                        if (reconnectDeadline) {
                            lj1005::TraceState("CompletionUsbReconnected", p.token.c_str());
                            reconnectDeadline = 0;
                        }
                        lj1005::TraceStatus(p.token.c_str(), response);
                        status.Feed(response);
                        if (status.GetCompletion()) {
                            finished = !status.GetCompletion()->cancelled;
                            lj1005::TraceState(finished ? "PrinterEnd" : "PrinterCancelled", p.token.c_str());
                            break;
                        }
                        Sleep(100);
                    }
                    if (!finished)
                        lj1005::TraceState("CompletionUnconfirmed", p.token.c_str());
                    if (!finished)
                        OutputDebugStringA(
                            "LaserJet 1005: no successful job-end report; retaining completion state without replaying data");
                }
                catch (const Error& error) {
                    lj1005::TraceState("CompletionFailure", p.token.c_str(), "Lower port write failed", error.code);
                }
                catch (const std::exception& error) {
                    lj1005::TraceState("CompletionFailure", p.token.c_str(), error.what());
                    OutputDebugStringA(error.what());
                }
                catch (...) {
                    lj1005::TraceState("CompletionFailure", p.token.c_str(), "Unknown exception");
                    OutputDebugStringA("LaserJet 1005: completion transport failed after document submission");
                }
            }
            p.active = false;
            const BOOL ended = p.lower.pfnEndDocPort(p.handle);
            const DWORD endError = GetLastError();
            lj1005::TraceState("LowerEndDoc", p.token.c_str(), ended ? "success" : "failure", ended ? 0 : endError);
            if (finished && ended) {
                try {
                    p.completion->Acknowledge();
                    lj1005::TraceState("CompletionAcknowledged", p.token.c_str());
                }
                catch (const std::exception& error) {
                    lj1005::TraceState("AcknowledgementFailure", p.token.c_str(), error.what());
                    OutputDebugStringA(error.what());
                }
            }
            p.completion.reset();
            if (!ended)
                Fail(endError);
            if (invalid)
                Fail(ERROR_INVALID_DATA);
        });
    }

    BOOL WINAPI ClosePort(HANDLE handle) {
        return Protect([&] {
            auto& p = GetPort(handle);
            // Spooler closes only after its outstanding calls on this handle have returned.
            if (p.active)
                p.lower.pfnEndDocPort(p.handle);
            const BOOL result = p.lower.pfnClosePort(p.handle);
            const DWORD error = GetLastError();
            delete &p;
            if (!result)
                Fail(error);
        });
    }

    BOOL WINAPI ReadPort(HANDLE handle, LPBYTE bytes, DWORD size, LPDWORD read) {
        if (read)
            *read = 0;
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (!p.lower.pfnReadPort)
                Fail(ERROR_NOT_SUPPORTED);
            Checked(p.lower.pfnReadPort(p.handle, bytes, size, read));
        });
    }

    BOOL WINAPI GetDataFromPort(HANDLE handle, DWORD control, LPWSTR value, LPWSTR input, DWORD inputSize,
                                LPWSTR output, DWORD outputSize, LPDWORD returned) {
        if (returned)
            *returned = 0;
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (!p.lower.pfnGetPrinterDataFromPort)
                Fail(ERROR_NOT_SUPPORTED);
            Checked(p.lower.pfnGetPrinterDataFromPort(p.handle, control, value, input, inputSize, output, outputSize,
                                                      returned));
        });
    }

    BOOL WINAPI SetPortTimeouts(HANDLE handle, LPCOMMTIMEOUTS values, DWORD reserved) {
        return Protect([&] {
            auto& p = GetPort(handle);
            std::lock_guard lock(p.mutex);
            if (!p.lower.pfnSetPortTimeOuts)
                Fail(ERROR_NOT_SUPPORTED);
            Checked(p.lower.pfnSetPortTimeOuts(p.handle, values, reserved));
        });
    }

    void WINAPI Shutdown(HANDLE instance) {
        delete static_cast<Instance*>(instance);
    }

    DWORD WINAPI SendRecvBidiData(HANDLE handle, DWORD access, LPCWSTR action, PBIDI_REQUEST_CONTAINER request,
                                  PBIDI_RESPONSE_CONTAINER* response) {
        if (response)
            *response = nullptr;
        DWORD result = ERROR_BIDI_NOT_SUPPORTED;
        if (!Protect([&] {
                auto& p = GetPort(handle);
                std::lock_guard lock(p.mutex);
                if (!response)
                    Fail(ERROR_INVALID_PARAMETER);
                if (p.lower.pfnSendRecvBidiDataFromPort)
                    result = p.lower.pfnSendRecvBidiDataFromPort(p.handle, access, action, request, response);
            }))
            return GetLastError();
        return result;
    }

    MONITOR2 table = [] {
        MONITOR2 t{};
        t.cbSize = sizeof(t);
        t.pfnOpenPortEx = OpenPortEx;
        t.pfnStartDocPort = StartDocPort;
        t.pfnWritePort = WritePort;
        t.pfnReadPort = ReadPort;
        t.pfnEndDocPort = EndDocPort;
        t.pfnClosePort = ClosePort;
        t.pfnGetPrinterDataFromPort = GetDataFromPort;
        t.pfnSetPortTimeOuts = SetPortTimeouts;
        t.pfnShutdown = Shutdown;
        t.pfnSendRecvBidiDataFromPort = SendRecvBidiData;
        return t;
    }();
}

extern "C" LPMONITOR2 WINAPI InitializePrintMonitor2(PMONITORINIT init, PHANDLE out) {
    if (out)
        *out = nullptr;
    if (!init || init->cbSize < sizeof(MONITORINIT) || !out || !init->bLocal) {
        SetLastError(ERROR_NOT_SUPPORTED);
        return nullptr;
    }
    if (!Protect([&] { *out = new Instance; }))
        return nullptr;
    return &table;
}
