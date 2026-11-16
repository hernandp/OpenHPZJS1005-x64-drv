// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include "lj1005/protocol_constants.hpp"
#include <cstdint>
#include <iosfwd>
#include <span>
#include <vector>

namespace Lj1005 {
    using Bytes = std::vector<std::uint8_t>;

    struct Raster {
        std::uint32_t width{}, height{};
        Bytes pixels; // Top-down, MSB first, 1=black, ceil(width/8) bytes per row.
    };

    struct PageOptions {
        std::uint32_t paper = Protocol::A4_PAPER; // A4, Windows DMPAPER value.
        std::uint32_t copies = 1;
        std::uint32_t source = Protocol::AUTOMATIC_PAPER_SOURCE;
        std::uint32_t offsetX = 0, offsetY = 0;
    };

    Raster ReadPBM(std::istream& input);
    void WritePBM(std::ostream& output, const Raster& raster);
    Raster TestPage();
    Raster PlaceOnSheet(const Raster& raster, std::uint32_t width, std::uint32_t height, std::uint32_t x,
                        std::uint32_t y);
    Bytes BeginDocument();
    Bytes EncodePage(const Raster& raster, const PageOptions& options, std::uint32_t pageNumber);
    Bytes EndDocument();
    Bytes EncodeJob(const Raster& raster, const PageOptions& options = {});
    Bytes WrapFirmware(std::span<const std::uint8_t> elf);

    struct PrintJobInfo {
        std::uint32_t pages{}, sheets{};
        std::uint64_t rasterBytes{};
    };

    // Strictly validate the supported 1005 print profile, including decoding each image.
    // Throws before callers may send any bytes; retains at most one decoded page.
    PrintJobInfo ValidatePrintJob(std::span<const std::uint8_t> data);
    void InspectZjs(std::span<const std::uint8_t> data, std::ostream& output);
    std::vector<Raster> DecodeJob(std::span<const std::uint8_t> data);
}
