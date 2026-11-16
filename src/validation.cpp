// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/protocol.hpp"
extern "C" {
#include "jbig.h"
}
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace Lj1005 {
    using namespace Protocol;

    namespace {
        constexpr std::array<std::uint32_t, MAX_PAGE_ITEMS> ALLOWED_PAGE_TAGS{
            ItemTag::ECONOMY_MODE,   ItemTag::RESOLUTION_ENHANCEMENT,
            ItemTag::VIDEO_WIDTH,    ItemTag::VIDEO_HEIGHT,
            ItemTag::BITS_PER_PIXEL, ItemTag::RASTER_WIDTH,
            ItemTag::RASTER_HEIGHT,  ItemTag::OFFSET_X,
            ItemTag::OFFSET_Y,       ItemTag::CUSTOM_WIDTH,
            ItemTag::CUSTOM_HEIGHT,  ItemTag::IMAGE_ENTITIES,
            ItemTag::RESOLUTION_X,   ItemTag::RESOLUTION_Y,
            ItemTag::PAPER_SOURCE,   ItemTag::COPIES,
            ItemTag::PAPER,          ItemTag::MEDIA_TYPE,
            ItemTag::PAGE_NUMBER};

        [[noreturn]] void Invalid(const char* message) {
            throw std::runtime_error(message);
        }

        std::uint32_t Read32(std::span<const std::uint8_t> bytes, std::size_t offset) {
            if (offset > bytes.size() || bytes.size() - offset < 4)
                Invalid("Truncated ZJS field");
            return (std::uint32_t(bytes[offset]) << 24) | (std::uint32_t(bytes[offset + 1]) << 16) |
                   (std::uint32_t(bytes[offset + 2]) << 8) | bytes[offset + 3];
        }

        struct Chunk {
            std::uint32_t type;
            std::map<std::uint32_t, std::uint32_t> items;
            std::span<const std::uint8_t> payload;

            std::uint32_t Value(std::uint32_t tag) const {
                const auto found = items.find(tag);
                if (found == items.end())
                    Invalid("Missing required ZJS item");
                return found->second;
            }

            std::uint32_t Offset(std::uint32_t tag) const {
                return items.contains(tag) ? Value(tag) : 0;
            }

            void Empty() const {
                if (!items.empty() || !payload.empty())
                    Invalid("Unexpected data in ZJS boundary record");
            }
        };

        Chunk ReadChunk(std::span<const std::uint8_t> bytes, std::size_t& position) {
            const auto remaining = bytes.subspan(position);
            if (remaining.size() < RECORD_HEADER_BYTES)
                Invalid("Truncated ZJS record");
            const auto size = Read32(remaining, 0), count = Read32(remaining, RECORD_ITEM_COUNT_OFFSET);
            if (size < RECORD_HEADER_BYTES || size > remaining.size() || count > MAX_PAGE_ITEMS ||
                count > (size - RECORD_HEADER_BYTES) / UNSIGNED_ITEM_BYTES ||
                Read32(remaining, RECORD_RESERVED_OFFSET) != RECORD_SIGNATURE)
                Invalid("Invalid ZJS record header");
            Chunk chunk{Read32(remaining, RECORD_TYPE_OFFSET), {}, {}};
            std::size_t offset = RECORD_HEADER_BYTES;
            for (std::uint32_t i = 0; i < count; ++i, offset += UNSIGNED_ITEM_BYTES) {
                if (Read32(remaining, offset) != UNSIGNED_ITEM_BYTES ||
                    remaining[offset + ITEM_TYPE_OFFSET] != UNSIGNED_ITEM_TYPE ||
                    remaining[offset + ITEM_RESERVED_OFFSET] != 0)
                    Invalid("Only unsigned 32-bit ZJS items are supported");
                const auto tag =
                    (std::uint32_t(remaining[offset + ITEM_TAG_OFFSET]) << 8) | remaining[offset + ITEM_TAG_OFFSET + 1];
                if (!chunk.items.emplace(tag, Read32(remaining, offset + ITEM_VALUE_OFFSET)).second)
                    Invalid("Duplicate ZJS item");
            }
            chunk.payload = remaining.subspan(offset, size - offset);
            position += size;
            return chunk;
        }

        void ValidatePage(const Chunk& chunk, PrintJobInfo& info, std::uint32_t& width, std::uint32_t& height) {
            // The native encoder and foo2zjs -z0 -P profile share these fields.

            if (!chunk.payload.empty())
                Invalid("Unexpected START_PAGE payload");
            for (const auto& [tag, value] : chunk.items) {
                (void)value;
                if (std::find(ALLOWED_PAGE_TAGS.begin(), ALLOWED_PAGE_TAGS.end(), tag) == ALLOWED_PAGE_TAGS.end())
                    Invalid("Unsupported ZJS page item");
            }
            if (chunk.Value(ItemTag::ECONOMY_MODE) != ECONOMY_DISABLED ||
                chunk.Value(ItemTag::RESOLUTION_ENHANCEMENT) != ENHANCEMENT_ENABLED ||
                chunk.Value(ItemTag::BITS_PER_PIXEL) != MONOCHROME_BITS_PER_PIXEL ||
                chunk.Value(ItemTag::CUSTOM_WIDTH) != 0 || chunk.Value(ItemTag::CUSTOM_HEIGHT) != 0 ||
                chunk.Value(ItemTag::IMAGE_ENTITIES) != MONOCHROME_PLANES ||
                chunk.Value(ItemTag::RESOLUTION_X) != RESOLUTION_DPI ||
                chunk.Value(ItemTag::RESOLUTION_Y) != RESOLUTION_DPI ||
                chunk.Value(ItemTag::PAPER_SOURCE) != AUTOMATIC_PAPER_SOURCE ||
                chunk.Value(ItemTag::MEDIA_TYPE) != PLAIN_MEDIA)
                Invalid("Unsupported print settings; expected 600 dpi monochrome with automatic paper source");
            const auto paper = chunk.Value(ItemTag::PAPER), copies = chunk.Value(ItemTag::COPIES);
            if ((paper != LETTER_PAPER && paper != A4_PAPER) || copies == 0 || copies > MAX_COPIES)
                Invalid("Only A4/Letter and 1..999 copies are supported");
            width = chunk.Value(ItemTag::RASTER_WIDTH);
            height = chunk.Value(ItemTag::RASTER_HEIGHT);
            const auto sheetWidth = paper == LETTER_PAPER ? LETTER_WIDTH_PIXELS : A4_WIDTH_PIXELS,
                       sheetHeight = paper == LETTER_PAPER ? LETTER_HEIGHT_PIXELS : A4_HEIGHT_PIXELS;
            const auto x = chunk.Offset(ItemTag::OFFSET_X), y = chunk.Offset(ItemTag::OFFSET_Y);
            if (!width || !height || chunk.Value(ItemTag::VIDEO_WIDTH) != width ||
                chunk.Value(ItemTag::VIDEO_HEIGHT) != height || x > sheetWidth || y > sheetHeight ||
                width > sheetWidth - x || height > sheetHeight - y)
                Invalid("ZJS raster geometry does not fit the selected paper");
            if (chunk.Value(ItemTag::PAGE_NUMBER) != info.pages + 1)
                Invalid("ZJS page numbers must start at 1 and be consecutive");
            if (copies > MAX_SHEETS - info.sheets)
                Invalid("Print job exceeds 1000 sheets including copies");
            const auto rasterBytes = std::uint64_t((width + 7) / 8) * height;
            if (rasterBytes > MAX_JOB_BYTES - info.rasterBytes)
                Invalid("Print job exceeds 128 MiB of decoded rasters");
            ++info.pages;
            info.sheets += copies;
            info.rasterBytes += rasterBytes;
        }

        void ValidateImage(Bytes& compressed, std::uint32_t width, std::uint32_t height) {
            jbg_dec_state decoder{};
            jbg_dec_init(&decoder);

            struct Cleanup {
                jbg_dec_state* decoder;

                ~Cleanup() {
                    jbg_dec_free(decoder);
                }
            } cleanup{&decoder};

            jbg_dec_maxsize(&decoder, width, height);
            std::size_t used = 0;
            if (jbg_dec_in(&decoder, compressed.data(), compressed.size(), &used) != JBG_EOK ||
                jbg_dec_getplanes(&decoder) != MONOCHROME_PLANES || jbg_dec_getwidth(&decoder) != width ||
                jbg_dec_getheight(&decoder) != height ||
                jbg_dec_getsize(&decoder) != std::size_t((width + 7) / 8) * height)
                Invalid("Incomplete, corrupt or inconsistent JBIG image");
            // foo2zjs -z0 appends 16 zero bytes after rounding the final BID to four bytes.
            if (used < JBIG_HEADER_BYTES || used > compressed.size() ||
                compressed.size() - used !=
                    JBIG_PADDING_BYTES + (JBIG_ALIGNMENT_BYTES - used % JBIG_ALIGNMENT_BYTES) % JBIG_ALIGNMENT_BYTES ||
                !std::all_of(compressed.begin() + used, compressed.end(), [](auto byte) { return byte == 0; }))
                Invalid("Invalid JBIG padding or trailing image data");
        }
    }

    PrintJobInfo ValidatePrintJob(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > MAX_JOB_BYTES)
            Invalid("Print job exceeds 128 MiB input limit");
        if (bytes.size() < MAGIC.size() || Read32(bytes, 0) != MAGIC_BE)
            Invalid("Expected big-endian JZJZ stream");
        enum class Stage {
            document,
            page,
            image,
            data,
            pageEnd,
            finished
        };
        auto stage = Stage::document;
        PrintJobInfo info;
        Bytes compressed;
        std::uint32_t width = 0, height = 0, advertisedPages = 0, records = 0;
        std::size_t lastDataSize = 0;
        for (std::size_t position = MAGIC.size(); position < bytes.size();) {
            if (++records > MAX_RECORDS)
                Invalid("Print job exceeds ZJS record limit");
            const auto chunk = ReadChunk(bytes, position);
            if (chunk.type == Record::START_DOCUMENT && stage == Stage::document) {
                if (chunk.items.size() != DOCUMENT_ITEMS || !chunk.payload.empty() ||
                    chunk.Value(ItemTag::DOCUMENT_COLLATE) != UNCOLLATED ||
                    chunk.Value(ItemTag::DOCUMENT_DUPLEX) != SIMPLEX ||
                    chunk.Value(ItemTag::QUANTITY) != DOCUMENT_QUANTITY)
                    Invalid("Unsupported ZJS document settings");
                advertisedPages = chunk.Value(ItemTag::PAGE_COUNT);
                if (advertisedPages > MAX_SHEETS)
                    Invalid("Advertised page count exceeds print job limit");
                stage = Stage::page;
            }
            else if (chunk.type == Record::START_PAGE && stage == Stage::page) {
                ValidatePage(chunk, info, width, height);
                stage = Stage::image;
            }
            else if (chunk.type == Record::JBIG_HEADER && stage == Stage::image) {
                const auto header = chunk.payload;
                if (!chunk.items.empty() || header.size() != JBIG_HEADER_BYTES ||
                    Read32(header, 0) != JBIG_PROFILE_WORD || Read32(header, JBIG_WIDTH_OFFSET) != width ||
                    Read32(header, JBIG_HEIGHT_OFFSET) != height ||
                    Read32(header, JBIG_STRIPE_OFFSET) != JBIG_STRIPE_HEIGHT ||
                    Read32(header, JBIG_OPTIONS_OFFSET) != JBIG_OPTIONS_WORD)
                    Invalid("Unsupported JBIG header or mismatched page dimensions");
                compressed.assign(header.begin(), header.end());
                lastDataSize = 0;
                stage = Stage::data;
            }
            else if (chunk.type == Record::JBIG_DATA && stage == Stage::data) {
                if (!chunk.items.empty() || chunk.payload.empty() ||
                    chunk.payload.size() > JBIG_CHUNK_BYTES + JBIG_PADDING_BYTES ||
                    (lastDataSize && lastDataSize != JBIG_CHUNK_BYTES) ||
                    chunk.payload.size() > MAX_COMPRESSED_BYTES - compressed.size())
                    Invalid("Invalid JBIG data chunk size or sequence");
                compressed.insert(compressed.end(), chunk.payload.begin(), chunk.payload.end());
                lastDataSize = chunk.payload.size();
            }
            else if (chunk.type == Record::END_JBIG && stage == Stage::data) {
                chunk.Empty();
                if (!lastDataSize)
                    Invalid("Missing JBIG image data");
                ValidateImage(compressed, width, height);
                compressed.clear();
                stage = Stage::pageEnd;
            }
            else if (chunk.type == Record::END_PAGE && stage == Stage::pageEnd) {
                chunk.Empty();
                stage = Stage::page;
            }
            else if (chunk.type == Record::END_DOCUMENT && stage == Stage::page && info.pages) {
                chunk.Empty();
                if (advertisedPages && advertisedPages != info.pages)
                    Invalid("Advertised page count does not match the document");
                stage = Stage::finished;
            }
            else {
                Invalid("Unexpected ZJS record or document/page/image boundary");
            }
        }
        if (stage != Stage::finished)
            Invalid("Incomplete ZJS print document");
        return info;
    }
}
