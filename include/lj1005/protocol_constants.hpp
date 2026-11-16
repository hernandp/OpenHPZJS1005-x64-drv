// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace Lj1005::Protocol {
    // Wire values follow the original foo2zjs ZJS profile, not host enum ordinals.
    namespace Record {
        inline constexpr std::uint32_t START_DOCUMENT = 0;
        inline constexpr std::uint32_t END_DOCUMENT = 1;
        inline constexpr std::uint32_t START_PAGE = 2;
        inline constexpr std::uint32_t END_PAGE = 3;
        inline constexpr std::uint32_t JBIG_HEADER = 4;
        inline constexpr std::uint32_t JBIG_DATA = 5;
        inline constexpr std::uint32_t END_JBIG = 6;
    }

    namespace ItemTag {
        inline constexpr std::uint32_t PAGE_COUNT = 0;
        inline constexpr std::uint32_t DOCUMENT_COLLATE = 1;
        inline constexpr std::uint32_t DOCUMENT_DUPLEX = 2;
        inline constexpr std::uint32_t PAPER = 3;
        inline constexpr std::uint32_t COPIES = 4;
        inline constexpr std::uint32_t PAPER_SOURCE = 5;
        inline constexpr std::uint32_t MEDIA_TYPE = 6;
        inline constexpr std::uint32_t IMAGE_ENTITIES = 7;
        inline constexpr std::uint32_t RESOLUTION_X = 8;
        inline constexpr std::uint32_t RESOLUTION_Y = 9;
        inline constexpr std::uint32_t OFFSET_X = 10;
        inline constexpr std::uint32_t OFFSET_Y = 11;
        inline constexpr std::uint32_t RASTER_WIDTH = 12;
        inline constexpr std::uint32_t RASTER_HEIGHT = 13;
        inline constexpr std::uint32_t QUANTITY = 15;
        inline constexpr std::uint32_t BITS_PER_PIXEL = 16;
        inline constexpr std::uint32_t VIDEO_WIDTH = 17;
        inline constexpr std::uint32_t VIDEO_HEIGHT = 18;
        inline constexpr std::uint32_t RESOLUTION_ENHANCEMENT = 22;
        inline constexpr std::uint32_t ECONOMY_MODE = 23;
        inline constexpr std::uint32_t PAGE_NUMBER = 0x8110;
        inline constexpr std::uint32_t CUSTOM_WIDTH = 0x8113;
        inline constexpr std::uint32_t CUSTOM_HEIGHT = 0x8114;
    }

    inline constexpr std::array<std::uint8_t, 4> MAGIC{'J', 'Z', 'J', 'Z'};
    inline constexpr std::uint32_t MAGIC_BE = 0x4a5a4a5a;
    inline constexpr std::uint16_t RECORD_SIGNATURE = 0x5a5a;
    inline constexpr std::uint8_t RECORD_SIGNATURE_BYTE = 0x5a;
    inline constexpr std::uint8_t UNSIGNED_ITEM_TYPE = 1;
    inline constexpr std::size_t RECORD_HEADER_BYTES = 16;
    inline constexpr std::size_t ITEM_HEADER_BYTES = 8;
    inline constexpr std::size_t UNSIGNED_ITEM_BYTES = 12;
    inline constexpr std::size_t RECORD_TYPE_OFFSET = 4;
    inline constexpr std::size_t RECORD_ITEM_COUNT_OFFSET = 8;
    inline constexpr std::size_t RECORD_SIGNATURE_OFFSET = 14;
    inline constexpr std::size_t RECORD_RESERVED_OFFSET = 12;
    inline constexpr std::size_t ITEM_TAG_OFFSET = 4;
    inline constexpr std::size_t ITEM_TYPE_OFFSET = 6;
    inline constexpr std::size_t ITEM_RESERVED_OFFSET = 7;
    inline constexpr std::size_t ITEM_VALUE_OFFSET = 8;
    inline constexpr std::uint32_t LETTER_PAPER = 1;
    inline constexpr std::uint32_t A4_PAPER = 9;
    inline constexpr std::uint32_t AUTOMATIC_PAPER_SOURCE = 7;
    inline constexpr std::uint32_t PLAIN_MEDIA = 1;
    inline constexpr std::uint32_t SIMPLEX = 1;
    inline constexpr std::uint32_t MONOCHROME_PLANES = 1;
    inline constexpr std::uint32_t MONOCHROME_BITS_PER_PIXEL = 1;
    inline constexpr std::uint32_t RESOLUTION_DPI = 600;
    inline constexpr std::uint32_t ECONOMY_DISABLED = 0;
    inline constexpr std::uint32_t ENHANCEMENT_ENABLED = 1;
    inline constexpr std::uint32_t UNCOLLATED = 0;
    inline constexpr std::uint32_t DOCUMENT_QUANTITY = 1;
    inline constexpr std::uint32_t MAX_COPIES = 999;
    inline constexpr std::uint32_t MAX_RASTER_DIMENSION_PIXELS = 10000;
    inline constexpr std::uint32_t A4_WIDTH_PIXELS = 4960;
    inline constexpr std::uint32_t A4_HEIGHT_PIXELS = 7016;
    inline constexpr std::uint32_t LETTER_WIDTH_PIXELS = 5100;
    inline constexpr std::uint32_t LETTER_HEIGHT_PIXELS = 6600;
    inline constexpr std::size_t MAX_RASTER_BYTES = 32 * 1024 * 1024;
    inline constexpr std::size_t MAX_COMPRESSED_BYTES = 64 * 1024 * 1024;
    inline constexpr std::size_t MAX_JOB_BYTES = 128 * 1024 * 1024;
    inline constexpr std::uint32_t MAX_SHEETS = 1000;
    inline constexpr std::uint32_t MAX_RECORDS = 65536;
    inline constexpr std::size_t JBIG_HEADER_BYTES = 20;
    inline constexpr std::size_t JBIG_CHUNK_BYTES = 65536;
    inline constexpr std::size_t JBIG_PADDING_BYTES = 16;
    inline constexpr std::size_t JBIG_ALIGNMENT_BYTES = 4;
    inline constexpr std::size_t MAX_PAGE_ITEMS = 19;
    inline constexpr std::size_t DOCUMENT_ITEMS = 4;
    inline constexpr unsigned long JBIG_STRIPE_HEIGHT = 128;
    inline constexpr int JBIG_ADAPTIVE_TEMPLATE_X = 16;
    inline constexpr int JBIG_ADAPTIVE_TEMPLATE_Y = 0;
    inline constexpr std::uint32_t JBIG_PROFILE_WORD = 0x00000100;
    inline constexpr std::uint32_t JBIG_OPTIONS_WORD = 0x1000035c;
    inline constexpr std::size_t JBIG_WIDTH_OFFSET = 4;
    inline constexpr std::size_t JBIG_HEIGHT_OFFSET = 8;
    inline constexpr std::size_t JBIG_STRIPE_OFFSET = 12;
    inline constexpr std::size_t JBIG_OPTIONS_OFFSET = 16;
    inline constexpr std::size_t ELF_HEADER_BYTES = 52;
    inline constexpr std::size_t MAX_FIRMWARE_BYTES = 16 * 1024 * 1024;
}
