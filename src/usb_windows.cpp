// SPDX-License-Identifier: GPL-2.0-or-later
#include "lj1005/usb.hpp"
#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <initguid.h>
#include <usbprint.h>
#include <algorithm>
#include <cwctype>
#include <stdexcept>

namespace lj1005 {
    namespace {
        struct Handle {
            HANDLE value = INVALID_HANDLE_VALUE;

            ~Handle() {
                if (value != INVALID_HANDLE_VALUE && value)
                    CloseHandle(value);
            }

            Handle(const Handle&) = delete;

            explicit Handle(HANDLE h) : value(h) {}
        };

        [[noreturn]]
        void ThrowUsbError(const char* action) {
            throw std::runtime_error(std::string(action) + " (Win32 error " + std::to_string(GetLastError()) + ")");
        }

        bool IsLaserJet1005(std::wstring id) {
            std::transform(id.begin(), id.end(), id.begin(),
                           [](wchar_t c) { return static_cast<wchar_t>(std::towupper(c)); });
            return id.find(L"USB\\VID_03F0&PID_1317") == 0;
        }
    }

    std::vector<UsbDevice> GetUsbDevices() {
        const auto set =
            SetupDiGetClassDevsW(&GUID_DEVINTERFACE_USBPRINT, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (set == INVALID_HANDLE_VALUE)
            ThrowUsbError("Enumerating USB printers");

        struct Cleanup {
            HDEVINFO set;

            ~Cleanup() {
                SetupDiDestroyDeviceInfoList(set);
            }
        } cleanup{set};

        std::vector<UsbDevice> devices;
        for (DWORD index = 0;; ++index) {
            SP_DEVICE_INTERFACE_DATA iface{};
            iface.cbSize = sizeof(iface);
            if (!SetupDiEnumDeviceInterfaces(set, nullptr, &GUID_DEVINTERFACE_USBPRINT, index, &iface)) {
                if (GetLastError() == ERROR_NO_MORE_ITEMS)
                    break;
                ThrowUsbError("Enumerating USB interface");
            }
            DWORD needed = 0;
            SetupDiGetDeviceInterfaceDetailW(set, &iface, nullptr, 0, &needed, nullptr);
            if (!needed)
                ThrowUsbError("Sizing USB interface");
            std::vector<std::uint8_t> buffer(needed);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(*detail);
            SP_DEVINFO_DATA info{};
            info.cbSize = sizeof(info);
            if (!SetupDiGetDeviceInterfaceDetailW(set, &iface, detail, needed, nullptr, &info))
                ThrowUsbError("Reading USB interface");
            DEVINST node = info.DevInst;
            for (int depth = 0; depth < 4; ++depth) {
                wchar_t id[MAX_DEVICE_ID_LEN]{};
                if (CM_Get_Device_IDW(node, id, MAX_DEVICE_ID_LEN, 0) == CR_SUCCESS && IsLaserJet1005(id)) {
                    devices.push_back({detail->DevicePath, id});
                    break;
                }
                DEVINST parent{};
                if (CM_Get_Parent(&parent, node, 0) != CR_SUCCESS)
                    break;
                node = parent;
            }
        }
        return devices;
    }

    std::string GetUsbDeviceId(const UsbDevice& device) {
        Handle file(CreateFileW(device.path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                FILE_FLAG_OVERLAPPED, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
            ThrowUsbError("Opening USB device for identification");
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value)
            ThrowUsbError("Creating device-ID event");
        std::vector<unsigned char> buffer(4096, 0);
        DWORD returned = 0;
        OVERLAPPED op{};
        op.hEvent = event.value;
        if (!DeviceIoControl(file.value, IOCTL_USBPRINT_GET_1284_ID, nullptr, 0, buffer.data(),
                             static_cast<DWORD>(buffer.size()), &returned, &op)) {
            if (GetLastError() != ERROR_IO_PENDING)
                ThrowUsbError("Reading IEEE 1284 device ID");
            if (WaitForSingleObject(event.value, 5000) != WAIT_OBJECT_0) {
                CancelIoEx(file.value, &op);
                GetOverlappedResult(file.value, &op, &returned, TRUE);
                throw std::runtime_error("Timed out reading IEEE 1284 device ID");
            }
            if (!GetOverlappedResult(file.value, &op, &returned, FALSE))
                ThrowUsbError("Completing device ID read");
        }
        if (returned < 2 || returned > buffer.size())
            throw std::runtime_error("Invalid IEEE 1284 ID length");
        // Windows USBPRINT prepends a two-byte length field; use returned size to bound text.
        auto first = buffer.begin() + 2, last = buffer.begin() + returned;
        last = std::find(first, last, 0);
        return std::string(first, last);
    }

    Bytes UsbRead(const UsbDevice& device, std::uint32_t timeoutMs) {
        if (!timeoutMs || timeoutMs > 10000)
            throw std::runtime_error("USB read timeout must be 1..10000 ms");
        Handle file(CreateFileW(device.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
            ThrowUsbError("Opening USB status reader");
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value)
            ThrowUsbError("Creating USB read event");
        Bytes bytes(4096);
        DWORD received = 0;
        OVERLAPPED op{};
        op.hEvent = event.value;
        if (!ReadFile(file.value, bytes.data(), static_cast<DWORD>(bytes.size()), &received, &op)) {
            if (GetLastError() != ERROR_IO_PENDING)
                ThrowUsbError("Reading USB status");
            if (WaitForSingleObject(event.value, timeoutMs) != WAIT_OBJECT_0) {
                CancelIoEx(file.value, &op);
                if (!GetOverlappedResult(file.value, &op, &received, TRUE)) {
                    if (GetLastError() == ERROR_OPERATION_ABORTED)
                        return {};
                    ThrowUsbError("Completing cancelled USB status read");
                }
            }
            else if (!GetOverlappedResult(file.value, &op, &received, FALSE))
                ThrowUsbError("Completing USB status read");
        }
        if (received > bytes.size())
            throw std::runtime_error("Invalid USB read length");
        bytes.resize(received);
        return bytes;
    }

    void UsbSend(const UsbDevice& device, std::span<const std::uint8_t> bytes) {
        // Re-enumerate immediately before writing. Never accept an arbitrary device path.
        const auto present = GetUsbDevices();
        if (std::none_of(present.begin(), present.end(),
                         [&](const auto& d) { return d.path == device.path && d.instance == device.instance; }))
            throw std::runtime_error("The selected LaserJet 1005 is no longer present");
        Handle file(CreateFileW(device.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr));
        if (file.value == INVALID_HANDLE_VALUE)
            ThrowUsbError("Opening LaserJet USB interface");
        Handle event(CreateEventW(nullptr, TRUE, FALSE, nullptr));
        if (!event.value)
            ThrowUsbError("Creating USB completion event");
        for (std::size_t pos = 0; pos < bytes.size();) {
            const auto n = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - pos));
            OVERLAPPED op{};
            op.hEvent = event.value;
            ResetEvent(event.value);
            DWORD written = 0;
            if (!WriteFile(file.value, bytes.data() + pos, n, &written, &op)) {
                if (GetLastError() != ERROR_IO_PENDING)
                    ThrowUsbError("Writing USB data");
                if (WaitForSingleObject(event.value, 30000) != WAIT_OBJECT_0) {
                    CancelIoEx(file.value, &op);
                    // Keep the OVERLAPPED and data alive until cancellation has completed.
                    GetOverlappedResult(file.value, &op, &written, TRUE);
                    throw std::runtime_error(
                        "USB transfer timed out after 30 seconds; partial job may have reached printer");
                }
                if (!GetOverlappedResult(file.value, &op, &written, FALSE))
                    ThrowUsbError("Completing USB write");
            }
            if (!written || written > n)
                throw std::runtime_error("Invalid USB write count");
            pos += written;
        }
    }
}
