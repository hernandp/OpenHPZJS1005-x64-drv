// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "lj1005/runtime_constants.hpp"
#include "trace.hpp"
#include <TraceLoggingProvider.h>
#include <winmeta.h>
#include <algorithm>

TRACELOGGING_DEFINE_PROVIDER(lj1005Trace, "OpenZJS.LaserJet1005",
                             (0xe8d2763b, 0xf16e, 0x4b57, 0xb1, 0x4c, 0xc6, 0x37, 0xf7, 0x91, 0x50, 0x11));

namespace Lj1005 {
    namespace {
        constexpr ULONGLONG LIFECYCLE_KEYWORD = 1;
        constexpr ULONGLONG STATUS_KEYWORD = 2;
        SRWLOCK registrationLock = SRWLOCK_INIT;
        unsigned registrations = 0;
    }

    TraceRegistration::TraceRegistration() noexcept {
        const auto saved = GetLastError();
        AcquireSRWLockExclusive(&registrationLock);
        if (registrations++ == 0)
            TraceLoggingRegister(lj1005Trace);
        ReleaseSRWLockExclusive(&registrationLock);
        SetLastError(saved);
    }

    TraceRegistration::~TraceRegistration() {
        const auto saved = GetLastError();
        AcquireSRWLockExclusive(&registrationLock);
        if (--registrations == 0)
            TraceLoggingUnregister(lj1005Trace);
        ReleaseSRWLockExclusive(&registrationLock);
        SetLastError(saved);
    }

    void TraceState(const char* stage, const char* token, const char* detail, DWORD error) noexcept {
        const auto saved = GetLastError();
        TraceLoggingWrite(lj1005Trace, "MonitorState", TraceLoggingLevel(WINEVENT_LEVEL_INFO),
                          TraceLoggingKeyword(LIFECYCLE_KEYWORD), TraceLoggingString(stage, "Stage"),
                          TraceLoggingString(token, "Token"), TraceLoggingString(detail, "Detail"),
                          TraceLoggingUInt32(error, "Win32Error"));
        SetLastError(saved);
    }

    void TraceStatus(const char* token, std::span<const std::uint8_t> bytes) noexcept {
        if (bytes.empty())
            return;
        const auto saved = GetLastError();
        // Transport reads are bounded at 4096 bytes. Never trace document or firmware data.
        const auto size = static_cast<ULONG>(std::min<std::size_t>(bytes.size(), Runtime::STATUS_BUFFER_BYTES));
        TraceLoggingWrite(lj1005Trace, "PrinterStatus", TraceLoggingLevel(WINEVENT_LEVEL_VERBOSE),
                          TraceLoggingKeyword(STATUS_KEYWORD), TraceLoggingString(token, "Token"),
                          TraceLoggingBinary(bytes.data(), size, "Bytes"));
        SetLastError(saved);
    }
}
