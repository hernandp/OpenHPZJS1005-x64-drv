// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once
#include "protocol.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace Lj1005 {
    std::string PjlBeginJob(std::string_view token);
    std::string PjlEndJob(std::string_view token);

    struct JobCompletion {
        bool cancelled = false;
        std::uint32_t pages = 0;
    };

    // Incremental, bounded parser for HP's form-feed-delimited unsolicited job reports.
    // Reports for other job names and INFO/START/PAGE/ECHO messages never acknowledge this job.
    class PjlJobStatus {
        std::string token_, pending_;
        std::optional<JobCompletion> result_;
        bool setupReady_ = false;
        void ParseMessage(std::string_view message);

    public:
        explicit PjlJobStatus(std::string_view token);
        void Feed(std::span<const std::uint8_t> bytes);

        const std::optional<JobCompletion>& GetCompletion() const {
            return result_;
        }

        bool IsSetupReady() const {
            return setupReady_;
        }
    };
}
