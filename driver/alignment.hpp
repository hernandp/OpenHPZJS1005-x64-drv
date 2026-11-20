// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include "lj1005/protocol_constants.hpp"

namespace Lj1005::Alignment {
    enum class Units : std::uint32_t {
        millimeters,
        inches
    };
    inline constexpr std::int32_t MAX_SHIFT_UM = 200000;
    inline constexpr std::int32_t MICROMETERS_PER_MM = 1000;
    inline constexpr std::int32_t MICROMETERS_PER_INCH = 25400;
    inline constexpr int MM_DECIMALS = 3;
    inline constexpr int INCH_DECIMALS = 5;

    struct Shift {
        std::int32_t x = 0, y = 0;
    };

    inline bool IsValid(std::int32_t value) {
        return value >= -MAX_SHIFT_UM && value <= MAX_SHIFT_UM;
    }

    inline std::int64_t RoundRatio(std::int64_t numerator, std::int64_t denominator) {
        return (numerator + (numerator < 0 ? -denominator / 2 : denominator / 2)) / denominator;
    }

    inline Shift DeviceDots(Shift micrometers, bool landscape) {
        const auto dots = [](std::int32_t value) {
            return static_cast<std::int32_t>(
                RoundRatio(std::int64_t(value) * Protocol::RESOLUTION_DPI, MICROMETERS_PER_INCH));
        };
        // LANDSCAPE_CC270 rotates the viewed page clockwise into device coordinates.
        return landscape ? Shift{-dots(micrometers.y), dots(micrometers.x)}
                         : Shift{dots(micrometers.x), dots(micrometers.y)};
    }

    inline std::optional<std::int32_t> Parse(std::wstring_view text, Units units, std::wstring_view separator) {
        const bool negative = text.starts_with(L'-');
        if (negative || text.starts_with(L'+'))
            text.remove_prefix(1);
        const auto split = text.find(separator);
        const auto whole = text.substr(0, split);
        const auto fraction = split == text.npos ? std::wstring_view{} : text.substr(split + separator.size());
        const auto decimals = units == Units::inches ? INCH_DECIMALS : MM_DECIMALS;
        if (whole.size() > 3 || fraction.size() > static_cast<std::size_t>(decimals) ||
            (whole.empty() && fraction.empty()))
            return {};
        std::int64_t value = 0, divisor = 1;
        for (auto part : {whole, fraction}) {
            for (const auto digit : part) {
                if (digit < L'0' || digit > L'9')
                    return {};
                value = value * 10 + (digit - L'0');
            }
        }
        for (std::size_t i = 0; i < fraction.size(); ++i)
            divisor *= 10;
        const auto scale = units == Units::inches ? MICROMETERS_PER_INCH : MICROMETERS_PER_MM;
        const auto rounded = RoundRatio(value * scale, divisor);
        if (rounded > MAX_SHIFT_UM)
            return {};
        return static_cast<std::int32_t>(negative ? -rounded : rounded);
    }

    inline std::wstring Format(std::int32_t value, Units units, std::wstring_view separator) {
        const auto decimals = units == Units::inches ? INCH_DECIMALS : MM_DECIMALS;
        const auto scale = units == Units::inches ? MICROMETERS_PER_INCH : MICROMETERS_PER_MM;
        std::int64_t multiplier = 1;
        for (int i = 0; i < decimals; ++i)
            multiplier *= 10;
        const auto magnitude = value < 0 ? -std::int64_t(value) : std::int64_t(value);
        const auto scaled = RoundRatio(magnitude * multiplier, scale);
        auto text = std::wstring(value < 0 ? L"-" : L"") + std::to_wstring(scaled / multiplier);
        if (const auto remainder = scaled % multiplier) {
            auto fraction = std::to_wstring(multiplier + remainder).substr(1);
            while (fraction.back() == L'0')
                fraction.pop_back();
            text.append(separator).append(fraction);
        }
        return text;
    }
}
