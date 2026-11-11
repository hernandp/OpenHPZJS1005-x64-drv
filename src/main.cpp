// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/protocol.hpp"
#include "lj1005/firmware.hpp"
#include "lj1005/pjl.hpp"
#ifdef _WIN32
#include "lj1005/usb.hpp"
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <chrono>
#include <thread>

namespace {

    constexpr auto FILESIZE_LIMIT = 128 * 1024 * 1024;

    lj1005::Bytes ReadFile(const std::filesystem::path& path) {

        std::ifstream in(path, std::ios::binary | std::ios::ate);
        if (!in)
            throw std::runtime_error("Cannot open input file");

        const auto n = in.tellg();
        if (n < 0 || n > FILESIZE_LIMIT)
            throw std::runtime_error("Input exceeds 128 MiB limit");

        lj1005::Bytes b(static_cast<std::size_t>(n));
        in.seekg(0);
        in.read(reinterpret_cast<char*>(b.data()), static_cast<std::streamsize>(b.size()));

        if (!in)
            throw std::runtime_error("Failed reading input file");

        return b;
    }

    void WriteFile(const std::filesystem::path& path, const lj1005::Bytes& b) {

        std::ofstream out(path, std::ios::binary | std::ios::trunc);

        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        out.close();
        if (!out)
            throw std::runtime_error("Failed writing output file");

        std::cout << "Wrote " << b.size() << " bytes\n";
    }
}

int main(int argc, char** argv) {
    try {
        const std::string cmd = argc > 1 ? argv[1] : "";

        if (cmd == "testpage" && argc == 3) {
            std::ofstream out(argv[2], std::ios::binary);
            lj1005::WritePBM(out, lj1005::TestPage());
            return 0;
        }

        if (cmd == "encode" && (argc == 4 || argc == 5)) {
            lj1005::PageOptions options;
            if (argc == 5) {
                const std::string paper = argv[4];

                if (paper != "a4" && paper != "letter")
                    throw std::runtime_error("Paper must be a4 or letter");

                options.paper = paper == "letter" ? 1 : 9;
            }

            std::ifstream in(argv[2], std::ios::binary);
            if (!in)
                throw std::runtime_error("Cannot open PBM file");

            auto job = lj1005::BeginDocument();
            std::uint32_t page = 0;

            while (in.peek() != EOF) {
                auto r = lj1005::ReadPBM(in);
                auto p = lj1005::EncodePage(r, options, ++page);

                if (job.size() + p.size() > FILESIZE_LIMIT)
                    throw std::runtime_error("Job exceeds 128 MiB limit");

                job.insert(job.end(), p.begin(), p.end());
            }

            if (!page)
                throw std::runtime_error("No PBM pages");

            auto end = lj1005::EndDocument();
            job.insert(job.end(), end.begin(), end.end());

            WriteFile(argv[3], job);

            return 0;
        }

        if (cmd == "firmware" && argc == 4) {
            WriteFile(argv[3], lj1005::WrapFirmware(ReadFile(argv[2])));
            return 0;
        }

        if (cmd == "inspect" && argc == 3) {
            lj1005::InspectZjs(ReadFile(argv[2]), std::cout);
            return 0;
        }

        if (cmd == "validate" && argc == 3) {
            const auto info = lj1005::ValidatePrintJob(ReadFile(argv[2]));
            std::cout << "Validated " << info.pages << " page(s), " << info.sheets << " sheet(s), " << info.rasterBytes
                      << " raster bytes\n";
            return 0;
        }

        if (cmd == "decode" && argc == 4) {
            auto pages = lj1005::DecodeJob(ReadFile(argv[2]));

            std::ofstream out(argv[3], std::ios::binary);
            for (const auto& page : pages)
                lj1005::WritePBM(out, page);

            std::cout << "Decoded " << pages.size() << " page(s)\n";
            return 0;
        }

#ifdef _WIN32
        if (cmd == "usb-list" && argc == 2) {
            auto devices = lj1005::GetUsbDevices();
            for (const auto& d : devices)
                std::wcout << d.instance << L"\n" << d.path << L"\n";

            if (devices.empty())
                std::cout << "No HP LaserJet 1005 USB printer interface found\n";

            return devices.empty() ? 2 : 0;
        }

        if (cmd == "usb-id" && argc == 2) {
            auto devices = lj1005::GetUsbDevices();
            if (devices.size() != 1)
                throw std::runtime_error("Exactly one HP LaserJet 1005 is required");

            std::cout << lj1005::GetUsbDeviceId(devices.front()) << '\n';
            return 0;
        }

        if ((cmd == "usb-query-status" || cmd == "usb-read-status") && argc == 3) {

            const auto devices = lj1005::GetUsbDevices();

            if (devices.size() != 1)
                throw std::runtime_error("Exactly one HP LaserJet 1005 is required");

            if (lj1005::GetFirmwareState(lj1005::GetUsbDeviceId(devices.front())) != lj1005::FirmwareState::ready)
                throw std::runtime_error("Load firmware before querying PJL status");

            // Command found in the original HP zlmhp1 language monitor; no page data is sent.

            const std::string query =
                "\x1b%-12345X@PJL\r\n@PJL INFO STATUS\r\n@PJL ECHO SYNCSTATUS 1005\r\n@PJL ENTER LANGUAGE=ZJS\r\n";
            if (cmd == "usb-query-status")
                lj1005::UsbSend(devices.front(),
                                std::span(reinterpret_cast<const std::uint8_t*>(query.data()), query.size()));

            lj1005::Bytes response;
            for (unsigned i = 0; i < 5; ++i) {
                auto bytes = lj1005::UsbRead(devices.front());
                if (bytes.empty()) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                    continue;
                }
                response.insert(response.end(), bytes.begin(), bytes.end());
            }
            WriteFile(argv[2], response);
            std::cout << "Captured PJL status response; no print page submitted\n";
            return 0;
        }

        if ((cmd == "usb-print" || cmd == "usb-firmware") && argc == 3) {
            lj1005::Bytes bytes;
            if (cmd == "usb-print") {
                bytes = ReadFile(argv[2]);
                const auto info = lj1005::ValidatePrintJob(bytes);
                std::cout << "Validated " << info.pages << " page(s), " << info.sheets << " sheet(s)\n";
            }
            auto devices = lj1005::GetUsbDevices();
            if (devices.size() != 1)
                throw std::runtime_error("Exactly one connected HP LaserJet 1005 is required");

            if (cmd == "usb-firmware") {
                const auto& device = devices.front();
                const bool uploaded = lj1005::EnsureFirmware(
                    {[&] { return lj1005::GetUsbDeviceId(device); }, [&] { return ReadFile(argv[2]); },
                     [&](auto bytes) { lj1005::UsbSend(device, bytes); },
                     [] { std::this_thread::sleep_for(std::chrono::milliseconds(500)); }});
                std::cout << (uploaded ? "Firmware uploaded and readiness verified\n"
                                       : "Firmware already running; no upload needed\n");
                return 0;
            }
            if (lj1005::GetFirmwareState(lj1005::GetUsbDeviceId(devices.front())) != lj1005::FirmwareState::ready)
                throw std::runtime_error("Printer firmware is missing; run usb-firmware first");

            lj1005::UsbSend(devices.front(), bytes);
            std::cout << "Transferred " << bytes.size() << " bytes to USB; verify the printer physically\n";
            return 0;
        }
#endif
        std::cerr
            << "Usage:\n  lj1005 testpage output.pbm\n  lj1005 encode input.pbm output.zjs [a4|letter]\n"
               "  lj1005 inspect input.zjs\n  lj1005 validate input.zjs\n  lj1005 decode input.zjs output.pbm\n  lj1005 firmware sihp1005.img output.dl\n"
               "  lj1005 usb-list\n  lj1005 usb-id\n  lj1005 usb-query-status output.bin\n  lj1005 usb-read-status output.bin\n  lj1005 usb-firmware sihp1005.img\n  lj1005 usb-print input.zjs\n";
        return 1;
    }
    catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
}
