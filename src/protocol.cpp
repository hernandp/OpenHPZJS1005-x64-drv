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

namespace lj1005 {
    namespace {
        constexpr std::size_t MAX_RASTER_BYTES = 32 * 1024 * 1024;

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
            if (!w || !h || w > 10000 || h > 10000)
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
            AppendBE32(b, static_cast<std::uint32_t>(16 + items.size() * 12 + payload.size()));
            AppendBE32(b, type);
            AppendBE32(b, static_cast<std::uint32_t>(items.size()));
            AppendBE16(b, 0);
            AppendBE16(b, 0x5a5a);
            for (auto [tag, value] : items) {
                AppendBE32(b, 12);
                AppendBE16(b, static_cast<std::uint16_t>(tag));
                b.push_back(1);
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
                if (n > MAX_RASTER_BYTES * 2 || c.data.size() > MAX_RASTER_BYTES * 2 - n)
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
                if (s.size() == 32)
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
        Raster r{4960, 7016, Bytes(RasterSize(4960, 7016), 0)};
        const auto dot = [&](std::uint32_t x, std::uint32_t y) {
            r.pixels[y * 620 + x / 8] |= static_cast<std::uint8_t>(0x80u >> (x % 8));
        };
        for (std::uint32_t y = 300; y < 6716; ++y)
            for (std::uint32_t x = 300; x < 4660; ++x) {
                const bool border = x < 306 || x >= 4654 || y < 306 || y >= 6710;
                const bool bars = y >= 600 && y < 900 && x < 3900 && (x / 60) % 2 == 0;
                const bool checker = y >= 1200 && y < 1800 && x < 2100 && ((x / 30 + y / 30) % 2 == 0);
                const bool ruler = x < 360 && y % 236 < 4;
                if (border || bars || checker || ruler)
                    dot(x, y);
            }
        return r;
    }

    Bytes BeginDocument() {
        Bytes b{'J', 'Z', 'J', 'Z'};
        const std::array<Item, 4> items{{{1, 0}, {2, 1}, {0, 0}, {15, 1}}};
        AppendChunk(b, 0, items);
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
        if (!page || !o.copies || o.copies > 999 || (o.paper != 1 && o.paper != 9))
            throw std::runtime_error("Only A4/Letter and 1..999 copies are supported");
        if (o.offsetX > 10000 || o.offsetY > 10000)
            throw std::runtime_error("Invalid page offset");
        // JBIG-KIT's C API takes a mutable pointer. Own a copy and clear unused tail bits.
        Bytes pixels = r.pixels;
        if (r.width % 8)
            for (std::uint32_t y = 0; y < r.height; ++y)
                pixels[(y + 1) * ((r.width + 7) / 8) - 1] &= static_cast<std::uint8_t>(0xffu << (8 - r.width % 8));
        unsigned char* planes[]{pixels.data()};
        Compressed c;
        jbg_enc_state state{};
        jbg_enc_init(&state, r.width, r.height, 1, planes, Collect, &c);
        jbg_enc_options(&state, JBG_ILEAVE | JBG_SMID, JBG_DELAY_AT | JBG_LRLTWO | JBG_TPDON | JBG_TPBON | JBG_DPON,
                        128, 16, 0);
        jbg_enc_out(&state);
        jbg_enc_free(&state);
        if (c.error)
            std::rethrow_exception(c.error);
        if (c.data.size() <= 20)
            throw std::runtime_error("JBIG encoder produced no image data");
        std::vector<Item> items{{23, 0}, {22, 1},       {17, r.width}, {18, r.height},
                                {16, 1}, {12, r.width}, {13, r.height}};
        if (o.offsetX)
            items.emplace_back(10, o.offsetX);
        if (o.offsetY)
            items.emplace_back(11, o.offsetY);
        const std::array<Item, 10> rest{{{0x8113, 0},
                                         {0x8114, 0},
                                         {7, 1},
                                         {8, 600},
                                         {9, 600},
                                         {5, o.source},
                                         {4, o.copies},
                                         {3, o.paper},
                                         {6, 1},
                                         {0x8110, page}}};
        items.insert(items.end(), rest.begin(), rest.end());
        Bytes b;
        AppendChunk(b, 2, items);
        AppendChunk(b, 4, {}, std::span(c.data).first(20));
        // The 1005 PPD selects -P: no START_PLANE / END_PLANE records.
        for (std::size_t pos = 20; pos < c.data.size();) {
            const auto n = std::min<std::size_t>(65536, c.data.size() - pos);
            Bytes payload(c.data.begin() + pos, c.data.begin() + pos + n);
            pos += n;
            if (pos == c.data.size())
                payload.resize((n + 3) / 4 * 4 + 16, 0);
            AppendChunk(b, 5, {}, payload);
        }
        AppendChunk(b, 6);
        AppendChunk(b, 3);
        return b;
    }

    Bytes EndDocument() {
        Bytes b;
        AppendChunk(b, 1);
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
        if (elf.size() < 52 || elf.size() > 16 * 1024 * 1024 || elf.size() % 2 || elf[0] != 0x7f || elf[1] != 'E' ||
            elf[2] != 'L' || elf[3] != 'F' || elf[4] != 1 || elf[5] != 2 || elf[6] != 1 || elf[18] != 0 ||
            elf[19] != 40)
            throw std::runtime_error("Expected an even-sized big-endian ARM ELF32 firmware image");
        Bytes b{0xbe, 0xef, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H'};
        AppendBE32(b, static_cast<std::uint32_t>(elf.size() + 16));
        b.insert(b.end(), 3, 0);
        const std::array<std::uint8_t, 12> section{0xc0, 0xde, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0, 0};
        b.insert(b.end(), section.begin(), section.end());
        b.insert(b.end(), elf.begin(), elf.end());
        std::uint64_t sum = 0;
        for (std::size_t i = 17; i < b.size(); i += 2)
            sum += b[i] | (std::uint32_t(b[i + 1]) << 8);
        while (sum >> 16)
            sum = (sum & 0xffff) + (sum >> 16);
        const auto checksum = static_cast<std::uint16_t>(~sum);
        b.push_back(0xff);
        b.push_back(0xff);
        b.push_back(static_cast<std::uint8_t>(checksum));
        b.push_back(static_cast<std::uint8_t>(checksum >> 8));
        return b;
    }

    void InspectZjs(std::span<const std::uint8_t> b, std::ostream& out) {
        if (b.size() < 4 || b[0] != 'J' || b[1] != 'Z' || b[2] != 'J' || b[3] != 'Z')
            throw std::runtime_error("Expected big-endian JZJZ stream");
        for (std::size_t p = 4; p < b.size();) {
            if (b.size() - p < 16)
                throw std::runtime_error("Truncated ZJS header");
            const auto size = ReadBE32(b, p), type = ReadBE32(b, p + 4), count = ReadBE32(b, p + 8);
            if (size < 16 || size > b.size() - p || b[p + 14] != 0x5a || b[p + 15] != 0x5a)
                throw std::runtime_error("Invalid ZJS chunk");
            out << p << ": type=" << type << " size=" << size << " items=" << count << '\n';
            std::size_t q = p + 16;
            for (std::uint32_t i = 0; i < count; ++i) {
                if (p + size - q < 8)
                    throw std::runtime_error("Truncated ZJS item");
                auto n = ReadBE32(b, q);
                if (n < 8 || n > p + size - q)
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
        for (std::size_t p = 4; p < b.size();) {
            const auto size = ReadBE32(b, p), type = ReadBE32(b, p + 4);
            if (type == 4) {
                if (image || size != 36)
                    throw std::runtime_error("Invalid JBIG header sequence");
                image = true;
                compressed.assign(b.begin() + p + 16, b.begin() + p + size);
                RasterSize(ReadBE32(compressed, 4), ReadBE32(compressed, 8));
                if (compressed[0] != 0 || compressed[1] != 0 || compressed[2] != 1)
                    throw std::runtime_error("Only single-plane, single-layer JBIG is supported");
            }
            else if (type == 5) {
                if (!image || compressed.size() + size - 16 > MAX_RASTER_BYTES * 2)
                    throw std::runtime_error("Invalid JBIG data sequence");
                compressed.insert(compressed.end(), b.begin() + p + 16, b.begin() + p + size);
            }
            else if (type == 6) {
                if (!image)
                    throw std::runtime_error("Missing JBIG header");
                jbg_dec_state dec{};
                jbg_dec_init(&dec);
                jbg_dec_maxsize(&dec, 10000, 10000);

                struct FreeDecoder {
                    jbg_dec_state* dec;

                    ~FreeDecoder() {
                        jbg_dec_free(dec);
                    }
                } cleanup{&dec};

                std::size_t used = 0;
                if (jbg_dec_in(&dec, compressed.data(), compressed.size(), &used) != JBG_EOK ||
                    jbg_dec_getplanes(&dec) != 1)
                    throw std::runtime_error("Unsupported or corrupt JBIG image");
                Raster r{static_cast<std::uint32_t>(jbg_dec_getwidth(&dec)),
                         static_cast<std::uint32_t>(jbg_dec_getheight(&dec)),
                         {}};
                const auto n = RasterSize(r.width, r.height);
                total += n;
                if (total > 128 * 1024 * 1024 || jbg_dec_getsize(&dec) != n)
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
