// SPDX-License-Identifier: GPL-2.0-or-later
#include "lj1005/protocol.hpp"
extern "C" {
#include "jbig.h"
}
#include <algorithm>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>

using namespace lj1005;

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

void RoundTrip(std::uint32_t w, std::uint32_t h, bool random) {
    Raster r{w, h, Bytes(std::size_t((w + 7) / 8) * h, 0)};
    std::mt19937 rng(1005);
    if (random)
        for (auto& b : r.pixels)
            b = static_cast<std::uint8_t>(rng());
    if (w % 8)
        for (std::uint32_t y = 0; y < h; ++y)
            r.pixels[(y + 1) * ((w + 7) / 8) - 1] &= static_cast<std::uint8_t>(0xffu << (8 - w % 8));
    auto job = EncodeJob(r);
    std::ostringstream description;
    InspectZjs(job, description);
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
        // Pixel placement crosses byte boundaries; unused source tail bits stay white.
        const auto sheet = PlaceOnSheet(Raster{3, 2, {0xbf, 0x5f}}, 13, 4, 7, 1);
        Require(sheet.pixels == Bytes({0, 0, 1, 0x40, 0, 0x80, 0, 0}), "Physical margin placement");
        Rejects([] { PlaceOnSheet(Raster{8, 1, {0xff}}, 8, 2, 1, 0); });
        Rejects([] { PlaceOnSheet(Raster{8, 1, {0xff}}, 8, 2, 0, 0xffffffffu); });
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
