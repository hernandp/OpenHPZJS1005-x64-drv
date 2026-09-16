// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <windows.h>
#include <winspool.h>
#include <winsplp.h>
#include "lj1005/protocol.hpp"
#include <memory>
#include <string_view>

namespace lj1005 {
    struct CompletionBackend {
        virtual ~CompletionBackend() = default;
        // Must return within a bounded transport timeout, including when no bytes arrive.
        virtual Bytes Read() = 0;
        virtual bool IsCancelled() = 0;
        virtual void Acknowledge() = 0;
    };

    std::unique_ptr<CompletionBackend> CreateCompletionBackend(std::wstring_view portName, HANDLE lowerHandle,
                                                               const MONITOR2& lower, LPWSTR printer, DWORD job);
}
