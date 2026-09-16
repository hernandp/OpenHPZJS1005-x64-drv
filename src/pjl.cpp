// SPDX-License-Identifier: GPL-2.0-or-later
#include "lj1005/pjl.hpp"
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace lj1005 {
    namespace {
        void ValidateToken(std::string_view token) {
            if (token.empty() || token.size() > 64 || !std::all_of(token.begin(), token.end(), [](unsigned char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-';
                }))
                throw std::runtime_error("Invalid PJL job token");
        }

        std::string_view Trim(std::string_view value) {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t' || value.front() == '\r'))
                value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t' || value.back() == '\r'))
                value.remove_suffix(1);
            return value;
        }
    }

    std::string PjlBeginJob(std::string_view token) {
        ValidateToken(token);
        return "\x1b%-12345X@PJL\r\n@PJL USTATUS JOB=ON\r\n@PJL JOB NAME=\"" + std::string(token) +
               "\"\r\n@PJL ECHO READY-" + std::string(token) + "\r\n@PJL ENTER LANGUAGE=ZJS\r\n";
    }

    std::string PjlEndJob(std::string_view token) {
        ValidateToken(token);
        return "\x1b%-12345X@PJL\r\n@PJL EOJ NAME=\"" + std::string(token) + "\"\r\n@PJL ENTER LANGUAGE=ZJS\r\n";
    }

    PjlJobStatus::PjlJobStatus(std::string_view token) : token_(token) {
        ValidateToken(token);
    }

    void PjlJobStatus::Feed(std::span<const std::uint8_t> bytes) {
        for (const auto byte : bytes) {
            if (byte == '\f') {
                ParseMessage(pending_);
                pending_.clear();
            }
            else {
                if (pending_.size() >= 8192)
                    throw std::runtime_error("PJL response exceeds 8 KiB frame limit");
                pending_.push_back(static_cast<char>(byte));
            }
        }
    }

    void PjlJobStatus::ParseMessage(std::string_view text) {
        if (result_)
            return;
        std::vector<std::string_view> lines;
        while (!text.empty()) {
            const auto end = text.find('\n');
            const auto line = Trim(text.substr(0, end));
            if (!line.empty())
                lines.push_back(line);
            if (end == std::string_view::npos)
                break;
            text.remove_prefix(end + 1);
        }
        if (lines.size() == 1 && lines[0] == "@PJL ECHO READY-" + token_) {
            setupReady_ = true;
            return;
        }
        if (lines.size() < 3 || lines[0] != "@PJL USTATUS JOB" || (lines[1] != "END" && lines[1] != "CANCELED"))
            return;
        std::optional<std::string_view> name, result;
        std::optional<std::uint32_t> pages;
        for (std::size_t i = 2; i < lines.size(); ++i) {
            const auto equal = lines[i].find('=');
            if (equal == std::string_view::npos)
                return;
            const auto key = Trim(lines[i].substr(0, equal)), value = Trim(lines[i].substr(equal + 1));
            if (key == "NAME") {
                if (name || value.size() < 2 || value.front() != '"' || value.back() != '"')
                    return;
                name = value.substr(1, value.size() - 2);
            }
            else if (key == "PAGES") {
                if (pages)
                    return;
                std::uint32_t number = 0;
                const auto conversion = std::from_chars(value.data(), value.data() + value.size(), number);
                if (conversion.ec != std::errc{} || conversion.ptr != value.data() + value.size())
                    return;
                pages = number;
            }
            else if (key == "RESULT") {
                if (result)
                    return;
                result = value;
            }
            else if (key != "ID")
                return;
        }
        if (!name || *name != token_)
            return;
        if (lines[1] == "CANCELED" || (result && *result != "OK")) {
            result_ = JobCompletion{true, pages.value_or(0)};
            return;
        }
        if (pages)
            result_ = JobCompletion{false, *pages};
    }
}
