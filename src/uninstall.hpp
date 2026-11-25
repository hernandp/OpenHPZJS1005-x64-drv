// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace Lj1005::Uninstall {
    struct Queue {
        std::wstring name;
        std::wstring port;
        std::uint32_t jobs;
        bool operator==(const Queue&) const = default;
    };

    struct State {
        std::wstring version;
        std::string catalogHash;
        std::wstring publishedInf;
        std::vector<Queue> queues;
        std::vector<std::filesystem::path> files;
        bool driver = false;
        bool monitor = false;
        bool testerTrust = false;

        bool HasDriverComponents() const;
        bool HasPendingJobs() const;
        bool operator==(const State&) const = default;
    };

    using Progress = std::function<void(const wchar_t*)>;

    State Inspect(const std::filesystem::path& package);
    State Remove(const std::filesystem::path& package, const State& confirmed, const Progress& progress);
    State RemoveTrust(const std::filesystem::path& package);
}
