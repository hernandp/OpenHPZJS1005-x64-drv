// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/pjl.hpp"
#include <iostream>
#include <stdexcept>
using namespace Lj1005;

void Require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}

void Feed(PjlJobStatus& parser, std::string_view text) {
    parser.Feed(std::span(reinterpret_cast<const std::uint8_t*>(text.data()), text.size()));
}

template <class F> void Rejects(F action) {
    bool threw = false;
    try {
        action();
    }
    catch (const std::exception&) {
        threw = true;
    }
    Require(threw, "Expected rejection");
}

int main() {
    try {
        const std::string end = "@PJL USTATUS JOB\r\nEND\r\nNAME=\"lj1005-21-1\"\r\nPAGES=3\r\n\f";
        // Exercise every split boundary, including splits inside NAME and the final terminator.
        for (std::size_t split = 0; split < end.size(); ++split) {
            PjlJobStatus parser("lj1005-21-1");
            Feed(parser, std::string_view(end).substr(0, split));
            Require(!parser.GetCompletion(), "Acknowledged an incomplete frame");
            Feed(parser, std::string_view(end).substr(split));
            Require(parser.GetCompletion() && !parser.GetCompletion()->cancelled && parser.GetCompletion()->pages == 3,
                    "Missing matching END");
        }
        PjlJobStatus parser("lj1005-21-1");
        const std::string echo = "@PJL ECHO READY-lj1005-21-1\r\n\f";
        for (std::size_t split = 0; split < echo.size(); ++split) {
            PjlJobStatus setup("lj1005-21-1");
            Feed(setup, "@PJL ECHO READY-other\r\n\f");
            Feed(setup, std::string_view(echo).substr(0, split));
            Require(!setup.IsSetupReady(), "Stale or incomplete setup echo accepted");
            Feed(setup, std::string_view(echo).substr(split));
            Require(setup.IsSetupReady() && !setup.GetCompletion(), "Setup echo confused with physical completion");
        }
        Feed(parser, "@PJL INFO STATUS\r\nCODE=10001\r\nONLINE=TRUE\r\n\f");
        Feed(parser, "@PJL USTATUS JOB\r\nSTART\r\nNAME=\"lj1005-21-1\"\r\n\f");
        Feed(parser, "@PJL USTATUS JOB\r\nEND\r\nNAME=\"other\"\r\nPAGES=3\r\n\f");
        Require(!parser.GetCompletion(), "Acknowledged INFO, START or a stale job");
        for (const auto field :
             {"PAGES=-1", "PAGES=4294967296", "PAGES=3garbage", "PAGES=3\r\nPAGES=4", "NAME=\"other\"\r\nPAGES=3"}) {
            PjlJobStatus invalid("lj1005-21-1");
            Feed(invalid, std::string("@PJL USTATUS JOB\r\nEND\r\nNAME=\"lj1005-21-1\"\r\n") + field + "\r\n\f");
            Require(!invalid.GetCompletion(), "Malformed completion accepted");
        }
        PjlJobStatus cancelled("lj1005-21-1");
        Feed(cancelled, "@PJL USTATUS JOB\r\nCANCELED\r\nNAME=\"lj1005-21-1\"\r\nRESULT=USER_CANCELED\r\n\f");
        Require(cancelled.GetCompletion() && cancelled.GetCompletion()->cancelled, "Cancellation reported as success");
        PjlJobStatus resultError("lj1005-21-1");
        Feed(resultError, "@PJL USTATUS JOB\r\nEND\r\nNAME=\"lj1005-21-1\"\r\nPAGES=0\r\nRESULT=USER_CANCELED\r\n\f");
        Require(resultError.GetCompletion() && resultError.GetCompletion()->cancelled,
                "Failed END reported as success");
        Rejects([] {
            PjlJobStatus overflow("token");
            Feed(overflow, std::string(8193, 'x'));
        });
        Rejects([] { PjlBeginJob("bad\"\r\n@PJL"); });
        Require(PjlBeginJob("token").ends_with("@PJL ENTER LANGUAGE=ZJS\r\n"), "Missing ZJS language selection");
        Require(PjlEndJob("token").find("@PJL EOJ NAME=\"token\"") != std::string::npos, "Missing EOJ token");
        // HP PJL job boundaries (Technical Reference, chapter 3): finish in PJL,
        // with a bare UEL last. Entering a language here starts another context
        // with no document; the next job must select its own language instead.
        Require(PjlEndJob("token").ends_with("\x1b%-12345X"), "Job trailer has no final UEL boundary");
        Require(PjlEndJob("token").find("ENTER LANGUAGE") == std::string::npos,
                "Job trailer re-enters a print language after EOJ");
        std::cout << "PJL completion: fragmented frames, matching jobs, malformed reports and cancellation passed\n";
        return 0;
    }
    catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
