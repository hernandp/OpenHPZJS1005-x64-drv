// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/firmware.hpp"
#include <iostream>
#include <stdexcept>
using namespace lj1005;

void Require(bool value) {
    if (!value)
        throw std::runtime_error("Firmware state test failed");
}

template <class F> void Rejects(F f) {
    bool failed = false;
    try {
        f();
    }
    catch (const std::exception&) {
        failed = true;
    }
    Require(failed);
}

int main() {
    try {
        const std::string cold = "MFG:Hewlett-Packard;MDL:hp LaserJet 1005 series;CMD:ZJS;";
        const std::string ready = cold + "FWVER:20021030;";
        Require(GetFirmwareState(cold) == FirmwareState::missing);
        Require(GetFirmwareState(ready) == FirmwareState::ready);
        Require(GetFirmwareState("FWVER:20021030;MDL:hp LaserJet 1005 series;MFG:Hewlett-Packard") ==
                FirmwareState::ready);
        Require(GetFirmwareState("ignored;" + ready) == FirmwareState::ready);
        Require(GetFirmwareState(ready + "ignored") == FirmwareState::ready);
        Require(GetFirmwareState(cold + "FWVER:;") == FirmwareState::missing);
        Rejects([&] { GetFirmwareState(ready + "MFG:Hewlett-Packard;"); });
        Rejects([&] { GetFirmwareState(ready + "MODEL:hp LaserJet 1005 series;"); });
        Rejects([] { GetFirmwareState("MFG:Hewlett-Packard;MDL:HP LaserJet P1005;FWVER:1;"); });
        Rejects([] { GetFirmwareState("MFG:EPSON;MDL:Other;"); });
        Rejects([&] { GetFirmwareState(ready + "FWVER:;"); });
        Rejects([] { GetFirmwareState(""); });
        unsigned loads = 0, writes = 0, waits = 0;
        Bytes elf(54, 0);
        elf[0] = 0x7f;
        elf[1] = 'E';
        elf[2] = 'L';
        elf[3] = 'F';
        elf[4] = 1;
        elf[5] = 2;
        elf[6] = 1;
        elf[19] = 40;
        FirmwareTransport t{[&] { return ready; },
                            [&] {
                                ++loads;
                                return elf;
                            },
                            [&](auto bytes) {
                                ++writes;
                                Require(bytes.size() == elf.size() + 33);
                            },
                            [&] { ++waits; }};
        Require(!EnsureFirmware(t));
        Require(loads == 0 && writes == 0 && waits == 0);
        t.identify = [&] {
            return waits >= 3 ? ready : cold;
        };
        Require(EnsureFirmware(t));
        Require(loads == 1 && writes == 1 && waits == 3);
        loads = writes = waits = 0;
        t.identify = [&] {
            return cold;
        };
        Rejects([&] { EnsureFirmware(t); });
        Require(loads == 1 && writes == 1 && waits == 20);
        loads = writes = waits = 0;
        t.identify = [] {
            return "MFG:Other;MDL:Other;";
        };
        Rejects([&] { EnsureFirmware(t); });
        Require(loads == 0 && writes == 0 && waits == 0);
        t.identify = [&] {
            return cold;
        };
        t.loadImage = [] {
            return Bytes(54, 0);
        };
        Rejects([&] { EnsureFirmware(t); });
        Require(writes == 0);
        std::cout << "Firmware state, wrong-device rejection, load-once and readiness timeout passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
