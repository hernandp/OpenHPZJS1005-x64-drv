// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/protocol.hpp"
extern "C" {
#include "jbig.h"
}
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace Lj1005;

namespace {
    constexpr std::uint32_t REPRODUCIBLE_SEED = 1005;
}

void Require(bool condition, const char* text) {
    if (!condition)
        throw std::runtime_error(text);
}

template <class F> void Rejects(F f) {
    bool caught = false;
    try {
        f();
    }
    catch (const std::exception&) {
        caught = true;
    }
    Require(caught, "Invalid input was accepted");
}

std::uint32_t ReadBE32(const Bytes& b, std::size_t p) {
    return (std::uint32_t(b.at(p)) << 24) | (std::uint32_t(b.at(p + 1)) << 16) | (std::uint32_t(b.at(p + 2)) << 8) |
           b.at(p + 3);
}

void WriteBE32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (24 - 8 * i));
}

std::size_t FindChunk(const Bytes& bytes, std::uint32_t type) {
    for (std::size_t p = 4; p < bytes.size(); p += ReadBE32(bytes, p))
        if (ReadBE32(bytes, p + 4) == type)
            return p;
    throw std::runtime_error("Test chunk missing");
}

void SetItem(Bytes& bytes, std::uint32_t type, std::uint32_t tag, std::uint32_t value) {
    const auto chunk = FindChunk(bytes, type);
    for (std::size_t p = chunk + 16; p < chunk + ReadBE32(bytes, chunk); p += 12)
        if ((ReadBE32(bytes, p + 4) >> 16) == tag) {
            WriteBE32(bytes, p + 8, value);
            return;
        }
    throw std::runtime_error("Test item missing");
}

void TestPrintValidation() {
    const Raster raster{13, 17, Bytes(34, 0)};
    const auto valid = EncodeJob(raster);
    const auto info = ValidatePrintJob(valid);
    Require(info.pages == 1 && info.sheets == 1 && info.rasterBytes == 34, "Print job summary");
    // Every prefix must fail, including cuts exactly at a complete record boundary.
    for (std::size_t size = 0; size < valid.size(); ++size)
        Rejects([&] { ValidatePrintJob(std::span(valid).first(size)); });
    auto empty = BeginDocument();
    const auto end = EndDocument();
    empty.insert(empty.end(), end.begin(), end.end());
    Rejects([&] { ValidatePrintJob(empty); });
    for (const auto type : {0u, 2u, 4u, 5u, 6u, 3u, 1u}) {
        auto missing = valid;
        const auto p = FindChunk(missing, type);
        const auto n = ReadBE32(missing, p);
        missing.erase(missing.begin() + p, missing.begin() + p + n);
        Rejects([&] { ValidatePrintJob(missing); });
        auto duplicated = valid;
        duplicated.insert(duplicated.begin() + p, valid.begin() + p, valid.begin() + p + n);
        Rejects([&] { ValidatePrintJob(duplicated); });
    }
    auto joined = valid;
    joined.insert(joined.end(), valid.begin(), valid.end());
    Rejects([&] { ValidatePrintJob(joined); });
    auto garbage = valid;
    garbage.push_back(0);
    Rejects([&] { ValidatePrintJob(garbage); });
    for (auto type : {7u, 8u, 9u, 10u, 0xffffffffu}) {
        auto bad = valid;
        WriteBE32(bad, FindChunk(bad, 2) + 4, type);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    // Item order has no meaning, but duplicate, missing and unrecognized tags do.
    auto reordered = valid;
    const auto page = FindChunk(valid, 2);
    std::swap_ranges(reordered.begin() + page + 16, reordered.begin() + page + 28, reordered.begin() + page + 28);
    ValidatePrintJob(reordered);
    for (auto tag : {22u, 99u}) {
        auto bad = valid;
        WriteBE32(bad, page + 20, (tag << 16) | 0x100);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    for (auto field : {0u, 8u, 12u}) {
        auto bad = valid;
        WriteBE32(bad, page + field, 0xffffffffu);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    for (auto field : {16u, 20u}) {
        auto bad = valid;
        WriteBE32(bad, page + field, 0);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    const std::pair<std::uint32_t, std::uint32_t> badItems[] = {
        {3, 0},   {4, 0},  {4, 1000},         {5, 0},      {6, 0},      {7, 2},     {8, 1200},
        {9, 300}, {12, 0}, {12, 0xffffffffu}, {13, 0},     {16, 2},     {17, 14},   {18, 18},
        {22, 0},  {23, 1}, {0x8110, 0},       {0x8110, 2}, {0x8113, 1}, {0x8114, 1}};
    for (auto [tag, value] : badItems) {
        auto bad = valid;
        SetItem(bad, 2, tag, value);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    for (auto tag : {1u, 2u, 15u}) {
        auto bad = valid;
        SetItem(bad, 0, tag, 2);
        Rejects([&] { ValidatePrintJob(bad); });
    }
    auto counted = valid;
    SetItem(counted, 0, 0, 1);
    ValidatePrintJob(counted);
    SetItem(counted, 0, 0, 2);
    Rejects([&] { ValidatePrintJob(counted); });
    const auto bih = FindChunk(valid, 4);
    for (std::size_t byte = 0; byte < 20; ++byte) {
        auto bad = valid;
        bad[bih + 16 + byte] ^= 1;
        Rejects([&] { ValidatePrintJob(bad); });
    }
    auto corrupt = valid;
    const auto bid = FindChunk(valid, 5);
    corrupt[bid + 16] = 0xff;
    corrupt[bid + 17] = 0x01; // JBIG ABORT, never a printable image.
    Rejects([&] { ValidatePrintJob(corrupt); });
    auto trailing = valid;
    trailing[bid + ReadBE32(valid, bid) - 1] = 1;
    Rejects([&] { ValidatePrintJob(trailing); });
    auto extra = valid;
    extra.insert(extra.begin() + bid + ReadBE32(valid, bid), 4, 0);
    WriteBE32(extra, bid, ReadBE32(valid, bid) + 4);
    Rejects([&] { ValidatePrintJob(extra); });
    PageOptions options;
    options.offsetX = 4960 - raster.width;
    options.offsetY = 7016 - raster.height;
    ValidatePrintJob(EncodeJob(raster, options));
    ++options.offsetX;
    const auto outside = EncodeJob(raster, options);
    Rejects([&] { ValidatePrintJob(outside); });
    const auto oversized = EncodeJob(Raster{4961, 1, Bytes(621, 0)});
    Rejects([&] { ValidatePrintJob(oversized); });
    options = {};
    options.paper = 1;
    options.copies = 999;
    const auto copies = ValidatePrintJob(EncodeJob(raster, options));
    Require(copies.sheets == 999, "Copy count summary");
    auto multi = BeginDocument();
    auto first = EncodePage(raster, options, 1);
    multi.insert(multi.end(), first.begin(), first.end());
    options.paper = 9;
    options.copies = 1;
    const auto second = EncodePage(raster, options, 2);
    multi.insert(multi.end(), second.begin(), second.end());
    multi.insert(multi.end(), end.begin(), end.end());
    const auto mixed = ValidatePrintJob(multi);
    Require(mixed.pages == 2 && mixed.sheets == 1000 && mixed.rasterBytes == 68, "Mixed media and copy budget");
    multi.resize(multi.size() - end.size());
    const auto third = EncodePage(raster, options, 3);
    multi.insert(multi.end(), third.begin(), third.end());
    multi.insert(multi.end(), end.begin(), end.end());
    Rejects([&] { ValidatePrintJob(multi); });
    // Blank pages compress well, so an input byte cap alone cannot bound decoder work.
    const Raster fullPage{4960, 7016, Bytes(620 * 7016, 0)};
    auto large = BeginDocument();
    auto pageJob = EncodeJob(fullPage);
    const auto pageStart = FindChunk(pageJob, 2), documentEnd = FindChunk(pageJob, 1);
    for (std::uint32_t number = 1; number <= 31; ++number) {
        SetItem(pageJob, 2, 0x8110, number);
        large.insert(large.end(), pageJob.begin() + pageStart, pageJob.begin() + documentEnd);
        if (number == 30) {
            large.insert(large.end(), end.begin(), end.end());
            Require(ValidatePrintJob(large).pages == 30, "Valid raster budget boundary");
            large.resize(large.size() - end.size());
        }
    }
    large.insert(large.end(), end.begin(), end.end());
    Rejects([&] { ValidatePrintJob(large); });
    std::cout << "Print validation passed (boundaries, items, geometry, JBIG integrity, copies and raster budget)\n";
}

void RoundTrip(std::uint32_t w, std::uint32_t h, bool random) {
    Raster r{w, h, Bytes(std::size_t((w + 7) / 8) * h, 0)};
    std::mt19937 rng(REPRODUCIBLE_SEED);
    if (random)
        for (auto& b : r.pixels)
            b = static_cast<std::uint8_t>(rng());
    if (w % 8)
        for (std::uint32_t y = 0; y < h; ++y)
            r.pixels[(y + 1) * ((w + 7) / 8) - 1] &= static_cast<std::uint8_t>(0xffu << (8 - w % 8));
    auto job = EncodeJob(r);
    std::ostringstream description;
    InspectZjs(job, description);
    ValidatePrintJob(job);
    Bytes compressed;
    unsigned dataChunks = 0;
    std::vector<unsigned> types;
    for (std::size_t p = 4; p < job.size();) {
        auto n = ReadBE32(job, p), type = ReadBE32(job, p + 4);
        types.push_back(type);
        if (type == 4 || type == 5)
            compressed.insert(compressed.end(), job.begin() + p + 16, job.begin() + p + n);
        if (type == 5) {
            ++dataChunks;
            Require(n <= 65536 + 16 + 16, "Oversized JBIG chunk");
        }
        Require(type != 9 && type != 10, "1005 must not receive plane markers");
        p += n;
    }
    Require(types.front() == 0 && types.back() == 1, "Document framing");
    if (random && w == 4960)
        Require(dataChunks > 1, "Chunk splitting not exercised");
    jbg_dec_state dec{};
    jbg_dec_init(&dec);
    std::size_t used = 0;
    auto result = jbg_dec_in(&dec, compressed.data(), compressed.size(), &used);
    Require(result == JBG_EOK, "JBIG decode failed");
    Require(jbg_dec_getwidth(&dec) == w && jbg_dec_getheight(&dec) == h, "JBIG geometry mismatch");
    Require(jbg_dec_getsize(&dec) == r.pixels.size(), "JBIG output size mismatch");
    Require(std::equal(r.pixels.begin(), r.pixels.end(), jbg_dec_getimage(&dec, 0)), "JBIG round trip changed pixels");
    jbg_dec_free(&dec);
}

int main() {
    try {
        for (auto first : {0x00, 0x09, 0x0a, 0x0d, 0x20, 0x23, 0xff}) {
            std::string data = "P4\n# comment\n8 1\n";
            data.push_back(static_cast<char>(first));
            std::istringstream in(data);
            Require(ReadPBM(in).pixels[0] == first, "PBM consumed first pixel byte");
        }
        Rejects([] {
            std::istringstream in("P4\n8 2\nX");
            ReadPBM(in);
        });
        Rejects([] {
            std::istringstream in("P4\n4294967295 2\n");
            ReadPBM(in);
        });
        Rejects([] { EncodeJob(Raster{8, 2, {0}}); });
        Rejects([] { WrapFirmware(Bytes(52, 0)); });
        Rejects([] {
            std::ostringstream out;
            InspectZjs(Bytes{'J', 'Z', 'J', 'Z', 0}, out);
        });
        RoundTrip(13, 17, true);
        RoundTrip(4960, 7016, false);
        RoundTrip(4960, 256, true);
        TestPrintValidation();
        // Pixel placement crosses byte boundaries; unused source tail bits stay white.
        const auto sheet = PlaceOnSheet(Raster{3, 2, {0xbf, 0x5f}}, 13, 4, 7, 1);
        Require(sheet.pixels == Bytes({0, 0, 1, 0x40, 0, 0x80, 0, 0}), "Physical margin placement");
        Rejects([] { PlaceOnSheet(Raster{8, 1, {0xff}}, 8, 2, 1, 0); });
        Rejects([] { PlaceOnSheet(Raster{8, 1, {0xff}}, 8, 2, 0, 0xffffffffu); });
        const Raster marks{3, 2, {0xa0, 0x60}};
        Require(PlaceOnSheet(marks, 5, 4, 1, 1, -2, -2).pixels == Bytes({0xc0, 0, 0, 0}),
                "Negative shifts clip the top and left");
        Require(PlaceOnSheet(marks, 5, 4, 1, 1, 3, 2).pixels == Bytes({0, 0, 0, 0x08}),
                "Positive shifts clip the right and bottom");
        Require(PlaceOnSheet(marks, 5, 4, 1, 1, 6, -6).pixels == Bytes(4, 0),
                "Shifting entirely off the sheet produces a blank page");
        Bytes elf(54, 0);
        elf[0] = 0x7f;
        elf[1] = 'E';
        elf[2] = 'L';
        elf[3] = 'F';
        elf[4] = 1;
        elf[5] = 2;
        elf[6] = 1;
        elf[19] = 40;
        auto fw = WrapFirmware(elf);
        Require(fw.size() == elf.size() + 33, "Firmware envelope length");
        Require(ReadBE32(fw, 10) == elf.size() + 16, "Firmware advertised length");
        std::uint32_t sum = 0;
        for (std::size_t p = 17; p < fw.size(); p += 2)
            sum += fw[p] | (std::uint32_t(fw[p + 1]) << 8);
        while (sum >> 16)
            sum = (sum & 65535) + (sum >> 16);
        Require(sum == 65535, "Firmware checksum");
        std::cout << "All protocol tests passed (PBM bounds, JBIG round trip, chunk splitting, firmware checksum)\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
