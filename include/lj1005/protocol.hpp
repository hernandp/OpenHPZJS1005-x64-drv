// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <cstdint>
#include <iosfwd>
#include <span>
#include <vector>

namespace lj1005 {
    using Bytes = std::vector<std::uint8_t>;

    struct Raster {
        std::uint32_t width{}, height{};
        Bytes pixels; // Top-down, MSB first, 1=black, ceil(width/8) bytes per row.
    };

    struct PageOptions {
        std::uint32_t paper = 9; // A4, Windows DMPAPER value.
        std::uint32_t copies = 1;
        std::uint32_t source = 7;
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
    void InspectZjs(std::span<const std::uint8_t> data, std::ostream& output);
    std::vector<Raster> DecodeJob(std::span<const std::uint8_t> data);
}
