// SPDX-License-Identifier: GPL-2.0-or-later
#include "lj1005/protocol.hpp"
extern "C" {
#include "jbig.h"
}
#include <algorithm>
#include <array>
#include <map>
#include <stdexcept>

namespace lj1005 {
    namespace {
        constexpr std::size_t MAX_JOB_BYTES = 128 * 1024 * 1024;
        constexpr std::size_t MAX_COMPRESSED_BYTES = 64 * 1024 * 1024;
        constexpr std::uint32_t MAX_SHEETS = 1000;
        constexpr std::uint32_t MAX_RECORDS = 65536;

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
            if (remaining.size() < 16)
                Invalid("Truncated ZJS record");
            const auto size = Read32(remaining, 0), count = Read32(remaining, 8);
            if (size < 16 || size > remaining.size() || count > 19 || count > (size - 16) / 12 ||
                Read32(remaining, 12) != 0x5a5a)
                Invalid("Invalid ZJS record header");
            Chunk chunk{Read32(remaining, 4), {}, {}};
            std::size_t offset = 16;
            for (std::uint32_t i = 0; i < count; ++i, offset += 12) {
                if (Read32(remaining, offset) != 12 || remaining[offset + 6] != 1 || remaining[offset + 7] != 0)
                    Invalid("Only unsigned 32-bit ZJS items are supported");
                const auto tag = (std::uint32_t(remaining[offset + 4]) << 8) | remaining[offset + 5];
                if (!chunk.items.emplace(tag, Read32(remaining, offset + 8)).second)
                    Invalid("Duplicate ZJS item");
            }
            chunk.payload = remaining.subspan(offset, size - offset);
            position += size;
            return chunk;
        }

        void ValidatePage(const Chunk& chunk, PrintJobInfo& info, std::uint32_t& width, std::uint32_t& height) {
            // The native encoder and foo2zjs -z0 -P profile share these fields.
            constexpr std::array<std::uint32_t, 19> tags{23,     22, 17, 18, 16, 12, 13, 10, 11,    0x8113,
                                                         0x8114, 7,  8,  9,  5,  4,  3,  6,  0x8110};
            if (!chunk.payload.empty())
                Invalid("Unexpected START_PAGE payload");
            for (const auto& [tag, value] : chunk.items) {
                (void)value;
                if (std::find(tags.begin(), tags.end(), tag) == tags.end())
                    Invalid("Unsupported ZJS page item");
            }
            if (chunk.Value(23) != 0 || chunk.Value(22) != 1 || chunk.Value(16) != 1 || chunk.Value(0x8113) != 0 ||
                chunk.Value(0x8114) != 0 || chunk.Value(7) != 1 || chunk.Value(8) != 600 || chunk.Value(9) != 600 ||
                chunk.Value(5) != 7 || chunk.Value(6) != 1)
                Invalid("Unsupported print settings; expected 600 dpi monochrome with automatic paper source");
            const auto paper = chunk.Value(3), copies = chunk.Value(4);
            if ((paper != 1 && paper != 9) || copies == 0 || copies > 999)
                Invalid("Only A4/Letter and 1..999 copies are supported");
            width = chunk.Value(12);
            height = chunk.Value(13);
            const auto sheetWidth = paper == 1 ? 5100u : 4960u, sheetHeight = paper == 1 ? 6600u : 7016u;
            const auto x = chunk.Offset(10), y = chunk.Offset(11);
            if (!width || !height || chunk.Value(17) != width || chunk.Value(18) != height || x > sheetWidth ||
                y > sheetHeight || width > sheetWidth - x || height > sheetHeight - y)
                Invalid("ZJS raster geometry does not fit the selected paper");
            if (chunk.Value(0x8110) != info.pages + 1)
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
                jbg_dec_getplanes(&decoder) != 1 || jbg_dec_getwidth(&decoder) != width ||
                jbg_dec_getheight(&decoder) != height ||
                jbg_dec_getsize(&decoder) != std::size_t((width + 7) / 8) * height)
                Invalid("Incomplete, corrupt or inconsistent JBIG image");
            // foo2zjs -z0 appends 16 zero bytes after rounding the final BID to four bytes.
            if (used < 20 || used > compressed.size() || compressed.size() - used != 16 + (4 - used % 4) % 4 ||
                !std::all_of(compressed.begin() + used, compressed.end(), [](auto byte) { return byte == 0; }))
                Invalid("Invalid JBIG padding or trailing image data");
        }
    }

    PrintJobInfo ValidatePrintJob(std::span<const std::uint8_t> bytes) {
        if (bytes.size() > MAX_JOB_BYTES)
            Invalid("Print job exceeds 128 MiB input limit");
        if (bytes.size() < 4 || Read32(bytes, 0) != 0x4a5a4a5a)
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
        for (std::size_t position = 4; position < bytes.size();) {
            if (++records > MAX_RECORDS)
                Invalid("Print job exceeds ZJS record limit");
            const auto chunk = ReadChunk(bytes, position);
            if (chunk.type == 0 && stage == Stage::document) {
                if (chunk.items.size() != 4 || !chunk.payload.empty() || chunk.Value(1) != 0 || chunk.Value(2) != 1 ||
                    chunk.Value(15) != 1)
                    Invalid("Unsupported ZJS document settings");
                advertisedPages = chunk.Value(0);
                if (advertisedPages > MAX_SHEETS)
                    Invalid("Advertised page count exceeds print job limit");
                stage = Stage::page;
            }
            else if (chunk.type == 2 && stage == Stage::page) {
                ValidatePage(chunk, info, width, height);
                stage = Stage::image;
            }
            else if (chunk.type == 4 && stage == Stage::image) {
                const auto header = chunk.payload;
                if (!chunk.items.empty() || header.size() != 20 || Read32(header, 0) != 0x00000100 ||
                    Read32(header, 4) != width || Read32(header, 8) != height || Read32(header, 12) != 128 ||
                    Read32(header, 16) != 0x1000035c)
                    Invalid("Unsupported JBIG header or mismatched page dimensions");
                compressed.assign(header.begin(), header.end());
                lastDataSize = 0;
                stage = Stage::data;
            }
            else if (chunk.type == 5 && stage == Stage::data) {
                if (!chunk.items.empty() || chunk.payload.empty() || chunk.payload.size() > 65552 ||
                    (lastDataSize && lastDataSize != 65536) ||
                    chunk.payload.size() > MAX_COMPRESSED_BYTES - compressed.size())
                    Invalid("Invalid JBIG data chunk size or sequence");
                compressed.insert(compressed.end(), chunk.payload.begin(), chunk.payload.end());
                lastDataSize = chunk.payload.size();
            }
            else if (chunk.type == 6 && stage == Stage::data) {
                chunk.Empty();
                if (!lastDataSize)
                    Invalid("Missing JBIG image data");
                ValidateImage(compressed, width, height);
                compressed.clear();
                stage = Stage::pageEnd;
            }
            else if (chunk.type == 3 && stage == Stage::pageEnd) {
                chunk.Empty();
                stage = Stage::page;
            }
            else if (chunk.type == 1 && stage == Stage::page && info.pages) {
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
