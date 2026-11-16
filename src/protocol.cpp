// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
// Protocol behavior adapted from OpenPrinting/foo2zjs; see THIRD_PARTY.md.
#include "lj1005/protocol.hpp"
extern "C" {
#include "jbig.h"
}
#include <algorithm>
#include <array>
#include <charconv>
#include <exception>
#include <istream>
#include <ostream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace Lj1005 {
    using namespace Protocol;

    namespace {
        constexpr std::size_t MAX_PBM_TOKEN_CHARS = 32;
        constexpr std::size_t JBIG_LOWEST_LAYER_OFFSET = 0;
        constexpr std::size_t JBIG_HIGHEST_LAYER_OFFSET = 1;
        constexpr std::size_t JBIG_PLANES_OFFSET = 2;
        constexpr std::uint8_t JBIG_BASE_LAYER = 0;
        constexpr std::uint8_t FIRMWARE_CHECKSUM_MARKER_BYTE = 0xff;
        constexpr std::uint32_t TEST_MARGIN_PIXELS = 300;
        constexpr std::uint32_t TEST_BORDER_PIXELS = 6;
        constexpr std::uint32_t BARS_TOP = 600;
        constexpr std::uint32_t BARS_BOTTOM = 900;
        constexpr std::uint32_t BARS_RIGHT = 3900;
        constexpr std::uint32_t BAR_WIDTH = 60;
        constexpr std::uint32_t CHECKER_TOP = 1200;
        constexpr std::uint32_t CHECKER_BOTTOM = 1800;
        constexpr std::uint32_t CHECKER_RIGHT = 2100;
        constexpr std::uint32_t CHECKER_CELL = 30;
        constexpr std::uint32_t RULER_WIDTH = 60;
        constexpr std::uint32_t RULER_TICK_SPACING = 236;
        constexpr std::uint32_t RULER_TICK_THICKNESS = 4;
        constexpr std::uint8_t ELF_MAGIC = 0x7f;
        constexpr std::uint8_t ELF32_CLASS = 1;
        constexpr std::uint8_t ELF_BIG_ENDIAN = 2;
        constexpr std::uint8_t ELF_VERSION = 1;
        constexpr std::uint8_t ARM_MACHINE = 40;
        constexpr std::size_t ELF_CLASS_OFFSET = 4;
        constexpr std::size_t ELF_ENDIAN_OFFSET = 5;
        constexpr std::size_t ELF_VERSION_OFFSET = 6;
        constexpr std::size_t ELF_MACHINE_OFFSET = 18;
        constexpr std::size_t FIRMWARE_LENGTH_OVERHEAD_BYTES = 16;
        constexpr std::size_t FIRMWARE_RESERVED_BYTES = 3;
        constexpr std::size_t FIRMWARE_CHECKSUM_START = 17;
        constexpr std::array<std::uint8_t, 10> FIRMWARE_MAGIC{0xbe, 0xef, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
        constexpr std::array<std::uint8_t, 12> FIRMWARE_SECTION{0xc0, 0xde, 'A', 'B', 'C', 'D',
                                                                'E',  'F',  'G', 'H', 0,   0};

        void AppendBE16(Bytes& b, std::uint16_t n) {
            b.push_back(static_cast<std::uint8_t>(n >> 8));
            b.push_back(static_cast<std::uint8_t>(n));
        }

        void AppendBE32(Bytes& b, std::uint32_t n) {
            AppendBE16(b, static_cast<std::uint16_t>(n >> 16));
            AppendBE16(b, static_cast<std::uint16_t>(n));
        }

        std::uint32_t ReadBE32(std::span<const std::uint8_t> b, std::size_t p) {
            if (p > b.size() || b.size() - p < 4)
                throw std::runtime_error("Truncated field");
            return (std::uint32_t(b[p]) << 24) | (std::uint32_t(b[p + 1]) << 16) | (std::uint32_t(b[p + 2]) << 8) |
                   b[p + 3];
        }

        std::size_t RasterSize(std::uint32_t w, std::uint32_t h) {
            if (!w || !h || w > MAX_RASTER_DIMENSION_PIXELS || h > MAX_RASTER_DIMENSION_PIXELS)
                throw std::runtime_error("Raster dimensions outside supported 1..10000 range");
            const auto size = std::size_t((w + 7) / 8) * h;
            if (size > MAX_RASTER_BYTES)
                throw std::runtime_error("Raster exceeds memory limit");
            return size;
        }

        void ValidateRaster(const Raster& r) {
            if (r.pixels.size() != RasterSize(r.width, r.height))
                throw std::runtime_error("Raster byte count does not match dimensions");
        }

        using Item = std::pair<std::uint32_t, std::uint32_t>;

        void AppendChunk(Bytes& b, std::uint32_t type, std::span<const Item> items = {},
                         std::span<const std::uint8_t> payload = {}) {
            AppendBE32(b, static_cast<std::uint32_t>(RECORD_HEADER_BYTES + items.size() * UNSIGNED_ITEM_BYTES +
                                                     payload.size()));
            AppendBE32(b, type);
            AppendBE32(b, static_cast<std::uint32_t>(items.size()));
            AppendBE16(b, 0);
            AppendBE16(b, RECORD_SIGNATURE);
            for (auto [tag, value] : items) {
                AppendBE32(b, UNSIGNED_ITEM_BYTES);
                AppendBE16(b, static_cast<std::uint16_t>(tag));
                b.push_back(UNSIGNED_ITEM_TYPE);
                b.push_back(0);
                AppendBE32(b, value);
            }
            b.insert(b.end(), payload.begin(), payload.end());
        }

        struct Compressed {
            Bytes data;
            std::exception_ptr error;
        };

        void Collect(unsigned char* p, std::size_t n, void* context) noexcept {
            auto& c = *static_cast<Compressed*>(context);
            if (c.error)
                return;
            try {
                if (n > MAX_COMPRESSED_BYTES || c.data.size() > MAX_COMPRESSED_BYTES - n)
                    throw std::runtime_error("Compressed data exceeds memory limit");
                c.data.insert(c.data.end(), p, p + n);
            }
            catch (...) {
                c.error = std::current_exception();
            }
        }

        bool IsWhitespace(int c) {
            return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
        }

        std::string ReadToken(std::istream& in) {
            for (;;) {
                while (IsWhitespace(in.peek()))
                    in.get();
                if (in.peek() != '#')
                    break;
                std::string ignored;
                std::getline(in, ignored);
            }
            std::string s;
            while (in.peek() != EOF && !IsWhitespace(in.peek()) && in.peek() != '#') {
                if (s.size() == MAX_PBM_TOKEN_CHARS)
                    throw std::runtime_error("PBM header token too long");
                s.push_back(static_cast<char>(in.get()));
            }
            if (s.empty())
                throw std::runtime_error("Missing PBM header token");
            return s;
        }

        std::uint32_t ParseNumber(const std::string& s) {
            std::uint32_t n{};
            auto [p, ec] = std::from_chars(s.data(), s.data() + s.size(), n);
            if (ec != std::errc{} || p != s.data() + s.size())
                throw std::runtime_error("Invalid PBM dimension");
            return n;
        }
    }

    Raster ReadPBM(std::istream& in) {
        if (ReadToken(in) != "P4")
            throw std::runtime_error("Expected binary PBM (P4)");
        Raster r;
        r.width = ParseNumber(ReadToken(in));
        r.height = ParseNumber(ReadToken(in));
        // Consume exactly the required separator. The first pixel byte may itself be whitespace or '#'.
        if (!IsWhitespace(in.get()))
            throw std::runtime_error("Missing PBM raster separator");
        r.pixels.resize(RasterSize(r.width, r.height));
        in.read(reinterpret_cast<char*>(r.pixels.data()), static_cast<std::streamsize>(r.pixels.size()));
        if (in.gcount() != static_cast<std::streamsize>(r.pixels.size()))
            throw std::runtime_error("Truncated PBM raster");
        return r;
    }

    void WritePBM(std::ostream& out, const Raster& r) {
        ValidateRaster(r);
        out << "P4\n" << r.width << ' ' << r.height << '\n';
        out.write(reinterpret_cast<const char*>(r.pixels.data()), static_cast<std::streamsize>(r.pixels.size()));
        if (!out)
            throw std::runtime_error("PBM write failed");
    }

    Raster TestPage() {
        Raster r{A4_WIDTH_PIXELS, A4_HEIGHT_PIXELS, Bytes(RasterSize(A4_WIDTH_PIXELS, A4_HEIGHT_PIXELS), 0)};
        const auto dot = [&](std::uint32_t x, std::uint32_t y) {
            r.pixels[y * ((A4_WIDTH_PIXELS + 7) / 8) + x / 8] |= static_cast<std::uint8_t>(0x80u >> (x % 8));
        };
        for (std::uint32_t y = TEST_MARGIN_PIXELS; y < A4_HEIGHT_PIXELS - TEST_MARGIN_PIXELS; ++y)
            for (std::uint32_t x = TEST_MARGIN_PIXELS; x < A4_WIDTH_PIXELS - TEST_MARGIN_PIXELS; ++x) {
                const bool border = x < TEST_MARGIN_PIXELS + TEST_BORDER_PIXELS ||
                                    x >= A4_WIDTH_PIXELS - TEST_MARGIN_PIXELS - TEST_BORDER_PIXELS ||
                                    y < TEST_MARGIN_PIXELS + TEST_BORDER_PIXELS ||
                                    y >= A4_HEIGHT_PIXELS - TEST_MARGIN_PIXELS - TEST_BORDER_PIXELS;
                const bool bars = y >= BARS_TOP && y < BARS_BOTTOM && x < BARS_RIGHT && (x / BAR_WIDTH) % 2 == 0;
                const bool checker = y >= CHECKER_TOP && y < CHECKER_BOTTOM && x < CHECKER_RIGHT &&
                                     ((x / CHECKER_CELL + y / CHECKER_CELL) % 2 == 0);
                const bool ruler =
                    x < TEST_MARGIN_PIXELS + RULER_WIDTH && y % RULER_TICK_SPACING < RULER_TICK_THICKNESS;
                if (border || bars || checker || ruler)
                    dot(x, y);
            }
        return r;
    }

    Bytes BeginDocument() {
        Bytes b(MAGIC.begin(), MAGIC.end());
        const std::array<Item, DOCUMENT_ITEMS> items{{{ItemTag::DOCUMENT_COLLATE, UNCOLLATED},
                                                      {ItemTag::DOCUMENT_DUPLEX, SIMPLEX},
                                                      {ItemTag::PAGE_COUNT, 0},
                                                      {ItemTag::QUANTITY, DOCUMENT_QUANTITY}}};
        AppendChunk(b, Record::START_DOCUMENT, items);
        return b;
    }

    Raster PlaceOnSheet(const Raster& r, std::uint32_t width, std::uint32_t height, std::uint32_t x, std::uint32_t y) {
        ValidateRaster(r);
        const auto size = RasterSize(width, height);
        if (x > width || y > height || r.width > width - x || r.height > height - y)
            throw std::runtime_error("Printable raster does not fit the physical sheet");
        Raster sheet{width, height, Bytes(size, 0)};
        const auto fromStride = (r.width + 7) / 8, toStride = (width + 7) / 8;
        for (std::uint32_t row = 0; row < r.height; ++row)
            for (std::uint32_t col = 0; col < r.width; ++col)
                if (r.pixels[std::size_t(row) * fromStride + col / 8] & (0x80u >> (col % 8)))
                    sheet.pixels[std::size_t(row + y) * toStride + (col + x) / 8] |=
                        static_cast<std::uint8_t>(0x80u >> ((col + x) % 8));
        return sheet;
    }

    Bytes EncodePage(const Raster& r, const PageOptions& o, std::uint32_t page) {
        ValidateRaster(r);
        if (!page || !o.copies || o.copies > MAX_COPIES || (o.paper != LETTER_PAPER && o.paper != A4_PAPER))
            throw std::runtime_error("Only A4/Letter and 1..999 copies are supported");
        if (o.offsetX > MAX_RASTER_DIMENSION_PIXELS || o.offsetY > MAX_RASTER_DIMENSION_PIXELS)
            throw std::runtime_error("Invalid page offset");
        // JBIG-KIT's C API takes a mutable pointer. Own a copy and clear unused tail bits.
        Bytes pixels = r.pixels;
        if (r.width % 8)
            for (std::uint32_t y = 0; y < r.height; ++y)
                pixels[(y + 1) * ((r.width + 7) / 8) - 1] &= static_cast<std::uint8_t>(0xffu << (8 - r.width % 8));
        unsigned char* planes[]{pixels.data()};
        Compressed c;
        jbg_enc_state state{};
        jbg_enc_init(&state, r.width, r.height, MONOCHROME_PLANES, planes, Collect, &c);
        jbg_enc_options(&state, JBG_ILEAVE | JBG_SMID, JBG_DELAY_AT | JBG_LRLTWO | JBG_TPDON | JBG_TPBON | JBG_DPON,
                        JBIG_STRIPE_HEIGHT, JBIG_ADAPTIVE_TEMPLATE_X, JBIG_ADAPTIVE_TEMPLATE_Y);
        jbg_enc_out(&state);
        jbg_enc_free(&state);
        if (c.error)
            std::rethrow_exception(c.error);
        if (c.data.size() <= JBIG_HEADER_BYTES)
            throw std::runtime_error("JBIG encoder produced no image data");
        std::vector<Item> items{{ItemTag::ECONOMY_MODE, ECONOMY_DISABLED},
                                {ItemTag::RESOLUTION_ENHANCEMENT, ENHANCEMENT_ENABLED},
                                {ItemTag::VIDEO_WIDTH, r.width},
                                {ItemTag::VIDEO_HEIGHT, r.height},
                                {ItemTag::BITS_PER_PIXEL, MONOCHROME_BITS_PER_PIXEL},
                                {ItemTag::RASTER_WIDTH, r.width},
                                {ItemTag::RASTER_HEIGHT, r.height}};
        if (o.offsetX)
            items.emplace_back(ItemTag::OFFSET_X, o.offsetX);
        if (o.offsetY)
            items.emplace_back(ItemTag::OFFSET_Y, o.offsetY);
        const std::array<Item, 10> rest{{{ItemTag::CUSTOM_WIDTH, 0},
                                         {ItemTag::CUSTOM_HEIGHT, 0},
                                         {ItemTag::IMAGE_ENTITIES, MONOCHROME_PLANES},
                                         {ItemTag::RESOLUTION_X, RESOLUTION_DPI},
                                         {ItemTag::RESOLUTION_Y, RESOLUTION_DPI},
                                         {ItemTag::PAPER_SOURCE, o.source},
                                         {ItemTag::COPIES, o.copies},
                                         {ItemTag::PAPER, o.paper},
                                         {ItemTag::MEDIA_TYPE, PLAIN_MEDIA},
                                         {ItemTag::PAGE_NUMBER, page}}};
        items.insert(items.end(), rest.begin(), rest.end());
        Bytes b;
        AppendChunk(b, Record::START_PAGE, items);
        AppendChunk(b, Record::JBIG_HEADER, {}, std::span(c.data).first(JBIG_HEADER_BYTES));
        // The 1005 PPD selects -P: no START_PLANE / END_PLANE records.
        for (std::size_t pos = JBIG_HEADER_BYTES; pos < c.data.size();) {
            const auto n = std::min<std::size_t>(JBIG_CHUNK_BYTES, c.data.size() - pos);
            Bytes payload(c.data.begin() + pos, c.data.begin() + pos + n);
            pos += n;
            if (pos == c.data.size())
                payload.resize((n + JBIG_ALIGNMENT_BYTES - 1) / JBIG_ALIGNMENT_BYTES * JBIG_ALIGNMENT_BYTES +
                                   JBIG_PADDING_BYTES,
                               0);
            AppendChunk(b, Record::JBIG_DATA, {}, payload);
        }
        AppendChunk(b, Record::END_JBIG);
        AppendChunk(b, Record::END_PAGE);
        return b;
    }

    Bytes EndDocument() {
        Bytes b;
        AppendChunk(b, Record::END_DOCUMENT);
        return b;
    }

    Bytes EncodeJob(const Raster& r, const PageOptions& o) {
        auto b = BeginDocument();
        auto p = EncodePage(r, o, 1);
        b.insert(b.end(), p.begin(), p.end());
        auto e = EndDocument();
        b.insert(b.end(), e.begin(), e.end());
        return b;
    }

    Bytes WrapFirmware(std::span<const std::uint8_t> elf) {
        // Only the ARM, ELF32, big-endian image format in the original 1005 package.
        if (elf.size() < ELF_HEADER_BYTES || elf.size() > MAX_FIRMWARE_BYTES || elf.size() % 2 || elf[0] != ELF_MAGIC ||
            elf[1] != 'E' || elf[2] != 'L' || elf[3] != 'F' || elf[ELF_CLASS_OFFSET] != ELF32_CLASS ||
            elf[ELF_ENDIAN_OFFSET] != ELF_BIG_ENDIAN || elf[ELF_VERSION_OFFSET] != ELF_VERSION ||
            elf[ELF_MACHINE_OFFSET] != 0 || elf[ELF_MACHINE_OFFSET + 1] != ARM_MACHINE)
            throw std::runtime_error("Expected an even-sized big-endian ARM ELF32 firmware image");
        Bytes b(FIRMWARE_MAGIC.begin(), FIRMWARE_MAGIC.end());
        AppendBE32(b, static_cast<std::uint32_t>(elf.size() + FIRMWARE_LENGTH_OVERHEAD_BYTES));
        b.insert(b.end(), FIRMWARE_RESERVED_BYTES, 0);
        b.insert(b.end(), FIRMWARE_SECTION.begin(), FIRMWARE_SECTION.end());
        b.insert(b.end(), elf.begin(), elf.end());
        std::uint64_t sum = 0;
        for (std::size_t i = FIRMWARE_CHECKSUM_START; i < b.size(); i += 2)
            sum += b[i] | (std::uint32_t(b[i + 1]) << 8);
        while (sum >> 16)
            sum = (sum & 0xffff) + (sum >> 16);
        const auto checksum = static_cast<std::uint16_t>(~sum);
        b.push_back(FIRMWARE_CHECKSUM_MARKER_BYTE);
        b.push_back(FIRMWARE_CHECKSUM_MARKER_BYTE);
        b.push_back(static_cast<std::uint8_t>(checksum));
        b.push_back(static_cast<std::uint8_t>(checksum >> 8));
        return b;
    }

    void InspectZjs(std::span<const std::uint8_t> b, std::ostream& out) {
        if (b.size() < MAGIC.size() || b[0] != 'J' || b[1] != 'Z' || b[2] != 'J' || b[3] != 'Z')
            throw std::runtime_error("Expected big-endian JZJZ stream");
        for (std::size_t p = MAGIC.size(); p < b.size();) {
            if (b.size() - p < RECORD_HEADER_BYTES)
                throw std::runtime_error("Truncated ZJS header");
            const auto size = ReadBE32(b, p), type = ReadBE32(b, p + RECORD_TYPE_OFFSET),
                       count = ReadBE32(b, p + RECORD_ITEM_COUNT_OFFSET);
            if (size < RECORD_HEADER_BYTES || size > b.size() - p ||
                b[p + RECORD_SIGNATURE_OFFSET] != RECORD_SIGNATURE_BYTE ||
                b[p + RECORD_SIGNATURE_OFFSET + 1] != RECORD_SIGNATURE_BYTE)
                throw std::runtime_error("Invalid ZJS chunk");
            out << p << ": type=" << type << " size=" << size << " items=" << count << '\n';
            std::size_t q = p + RECORD_HEADER_BYTES;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (p + size - q < ITEM_HEADER_BYTES)
                    throw std::runtime_error("Truncated ZJS item");
                auto n = ReadBE32(b, q);
                if (n < ITEM_HEADER_BYTES || n > p + size - q)
                    throw std::runtime_error("Invalid ZJS item length");
                q += n;
            }
            p += size;
        }
    }

    std::vector<Raster> DecodeJob(std::span<const std::uint8_t> b) {
        std::ostringstream validation;
        InspectZjs(b, validation);
        Bytes compressed;
        std::vector<Raster> pages;
        std::size_t total = 0;
        bool image = false;
        for (std::size_t p = MAGIC.size(); p < b.size();) {
            const auto size = ReadBE32(b, p), type = ReadBE32(b, p + RECORD_TYPE_OFFSET);
            if (type == Record::JBIG_HEADER) {
                if (image || size != RECORD_HEADER_BYTES + JBIG_HEADER_BYTES)
                    throw std::runtime_error("Invalid JBIG header sequence");
                image = true;
                compressed.assign(b.begin() + p + RECORD_HEADER_BYTES, b.begin() + p + size);
                RasterSize(ReadBE32(compressed, JBIG_WIDTH_OFFSET), ReadBE32(compressed, JBIG_HEIGHT_OFFSET));
                if (compressed[JBIG_LOWEST_LAYER_OFFSET] != JBIG_BASE_LAYER ||
                    compressed[JBIG_HIGHEST_LAYER_OFFSET] != JBIG_BASE_LAYER ||
                    compressed[JBIG_PLANES_OFFSET] != MONOCHROME_PLANES)
                    throw std::runtime_error("Only single-plane, single-layer JBIG is supported");
            }
            else if (type == Record::JBIG_DATA) {
                if (!image || compressed.size() + size - RECORD_HEADER_BYTES > MAX_COMPRESSED_BYTES)
                    throw std::runtime_error("Invalid JBIG data sequence");
                compressed.insert(compressed.end(), b.begin() + p + RECORD_HEADER_BYTES, b.begin() + p + size);
            }
            else if (type == Record::END_JBIG) {
                if (!image)
                    throw std::runtime_error("Missing JBIG header");
                jbg_dec_state dec{};
                jbg_dec_init(&dec);
                jbg_dec_maxsize(&dec, MAX_RASTER_DIMENSION_PIXELS, MAX_RASTER_DIMENSION_PIXELS);

                struct FreeDecoder {
                    jbg_dec_state* dec;

                    ~FreeDecoder() {
                        jbg_dec_free(dec);
                    }
                } cleanup{&dec};

                std::size_t used = 0;
                if (jbg_dec_in(&dec, compressed.data(), compressed.size(), &used) != JBG_EOK ||
                    jbg_dec_getplanes(&dec) != MONOCHROME_PLANES)
                    throw std::runtime_error("Unsupported or corrupt JBIG image");
                Raster r{static_cast<std::uint32_t>(jbg_dec_getwidth(&dec)),
                         static_cast<std::uint32_t>(jbg_dec_getheight(&dec)),
                         {}};
                const auto n = RasterSize(r.width, r.height);
                total += n;
                if (total > MAX_JOB_BYTES || jbg_dec_getsize(&dec) != n)
                    throw std::runtime_error("Decoded job exceeds limits");
                const auto* pixels = jbg_dec_getimage(&dec, 0);
                r.pixels.assign(pixels, pixels + n);
                pages.push_back(std::move(r));
                image = false;
            }
            p += size;
        }
        if (image || pages.empty())
            throw std::runtime_error("Incomplete ZJS image");
        return pages;
    }
}
