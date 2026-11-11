// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "completion_backend.hpp"
#include "lj1005/usb.hpp"
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace lj1005 {
    namespace {
        [[noreturn]] void ThrowCompletionError(const char* action, DWORD code = GetLastError()) {
            throw std::runtime_error(std::string(action) + " (Win32 " + std::to_string(code) + ")");
        }

        class Backend final : public CompletionBackend {
            HANDLE printer_ = nullptr, lowerHandle_;
            MONITOR2 lower_;
            DWORD job_;
            std::optional<UsbDevice> usb_;

        public:
            Backend(std::wstring_view portName, HANDLE handle, const MONITOR2& lower, LPWSTR printer, DWORD job)
                : lowerHandle_(handle), lower_(lower), job_(job) {
                if (portName.starts_with(L"USB")) {
                    const auto key =
                        std::wstring(L"SYSTEM\\CurrentControlSet\\Control\\Print\\Monitors\\USB Monitor\\Ports\\") +
                        std::wstring(portName);
                    std::array<wchar_t, 2048> path{};
                    DWORD bytes = static_cast<DWORD>(sizeof(path));
                    const auto result = RegGetValueW(HKEY_LOCAL_MACHINE, key.c_str(), L"Device Path", RRF_RT_REG_SZ,
                                                     nullptr, path.data(), &bytes);
                    if (result != ERROR_SUCCESS)
                        ThrowCompletionError("Reading USB port identity", result);
                    for (const auto& device : GetUsbDevices())
                        if (_wcsicmp(device.path.c_str(), path.data()) == 0) {
                            if (usb_)
                                throw std::runtime_error("Ambiguous USB printer interface");
                            usb_ = device;
                        }
                    if (!usb_)
                        throw std::runtime_error("The configured original LaserJet USB interface is not present");
                }
                else {
                    if (!lower_.pfnReadPort || !lower_.pfnSetPortTimeOuts)
                        throw std::runtime_error(
                            "Parallel completion requires a port monitor with timed bidirectional reads");
                    COMMTIMEOUTS timeouts{};
                    timeouts.ReadIntervalTimeout = MAXDWORD;
                    timeouts.ReadTotalTimeoutConstant = 500;
                    timeouts.WriteTotalTimeoutConstant = 30000;
                    if (!lower_.pfnSetPortTimeOuts(lowerHandle_, &timeouts, 0))
                        ThrowCompletionError("Setting parallel read timeout");
                }
                if (!OpenPrinterW(printer, &printer_, nullptr))
                    ThrowCompletionError("Opening printer for completion notification");
            }

            ~Backend() override {
                if (printer_)
                    ClosePrinter(printer_);
            }

            Bytes Read() override {
                // Independent overlapped USB reads give a bounded wait even if USBMON lacks timeout callbacks.
                // The underlying port monitor still owns all firmware and document writes.
                if (usb_)
                    return UsbRead(*usb_, 500);
                Bytes data(4096);
                DWORD count = 0;
                if (!lower_.pfnReadPort(lowerHandle_, data.data(), static_cast<DWORD>(data.size()), &count))
                    ThrowCompletionError("Reading parallel completion");
                if (count > data.size())
                    throw std::runtime_error("Invalid parallel read size");
                data.resize(count);
                return data;
            }

            bool IsCancelled() override {
                DWORD needed = 0;
                if (!GetJobW(printer_, job_, 1, nullptr, 0, &needed)) {
                    const auto code = GetLastError();
                    if (code == ERROR_INVALID_PARAMETER || code == ERROR_FILE_NOT_FOUND)
                        return true;
                    if (code != ERROR_INSUFFICIENT_BUFFER)
                        ThrowCompletionError("Sizing print job status", code);
                }
                if (!needed || needed > 1024 * 1024)
                    throw std::runtime_error("Invalid print job status size");
                std::vector<unsigned char> buffer(needed);
                if (!GetJobW(printer_, job_, 1, buffer.data(), needed, &needed)) {
                    const auto code = GetLastError();
                    if (code == ERROR_INVALID_PARAMETER || code == ERROR_FILE_NOT_FOUND)
                        return true;
                    ThrowCompletionError("Reading print job status", code);
                }
                const auto status = reinterpret_cast<JOB_INFO_1W*>(buffer.data())->Status;
                return (status & (JOB_STATUS_DELETING | JOB_STATUS_DELETED | JOB_STATUS_RESTART)) != 0;
            }

            void Acknowledge() override {
                if (!SetJobW(printer_, job_, 0, nullptr, JOB_CONTROL_LAST_PAGE_EJECTED)) {
                    const auto code = GetLastError();
                    // Deletion can race a real printer completion message.
                    if (code != ERROR_INVALID_PARAMETER && code != ERROR_FILE_NOT_FOUND)
                        ThrowCompletionError("Acknowledging printed job", code);
                }
            }
        };
    }

    std::unique_ptr<CompletionBackend> CreateCompletionBackend(std::wstring_view name, HANDLE handle,
                                                               const MONITOR2& lower, LPWSTR printer, DWORD job) {
        return std::make_unique<Backend>(name, handle, lower, printer, job);
    }
}
