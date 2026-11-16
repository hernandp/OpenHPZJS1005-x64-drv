// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
// Linked only into the test fixture DLL, never the installed monitor.
#include "../driver/completion_backend.hpp"
#include "lj1005/usb.hpp"
#include <stdexcept>
#include "completion_scenarios.hpp"

namespace {
    constexpr DWORD TRANSIENT_FAILURE_READS = 3;
    constexpr DWORD CANCEL_AFTER_READS = 2;
    DWORD acknowledgements = 0, scenario = CompletionScenario::NORMAL, reads = 0;

    class Fixture final : public Lj1005::CompletionBackend {
        HANDLE handle_;
        MONITOR2 lower_;

    public:
        Fixture(HANDLE handle, const MONITOR2& lower) : handle_(handle), lower_(lower) {}

        Lj1005::Bytes Read() override {
            ++reads;
            if ((scenario == CompletionScenario::RECONNECT && reads <= TRANSIENT_FAILURE_READS) ||
                scenario == CompletionScenario::CANCEL_DURING_DISCONNECT ||
                scenario == CompletionScenario::PERSISTENT_DISCONNECT)
                throw Lj1005::UsbReadUnavailable("Simulated USB disconnect");
            if (scenario == CompletionScenario::READ_FAILURE)
                throw std::runtime_error("Simulated status-read failure");
            Lj1005::Bytes bytes(Lj1005::Runtime::STATUS_BUFFER_BYTES);
            DWORD count = 0;
            if (!lower_.pfnReadPort ||
                !lower_.pfnReadPort(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &count))
                throw std::runtime_error("Fixture read failed");
            bytes.resize(count);
            return bytes;
        }

        bool IsCancelled() override {
            return scenario == CompletionScenario::CANCELLED ||
                   (scenario == CompletionScenario::CANCEL_DURING_DISCONNECT && reads >= CANCEL_AFTER_READS);
        }

        void Acknowledge() override {
            ++acknowledgements;
        }
    };
}

extern "C" __declspec(dllexport) DWORD WINAPI TestAcknowledgements() {
    return acknowledgements;
}

extern "C" __declspec(dllexport) void WINAPI TestCompletionScenario(DWORD value) {
    scenario = value;
    acknowledgements = 0;
    reads = 0;
}

namespace Lj1005 {
    std::unique_ptr<CompletionBackend> CreateCompletionBackend(std::wstring_view, HANDLE handle, const MONITOR2& lower,
                                                               LPWSTR, DWORD) {
        return std::make_unique<Fixture>(handle, lower);
    }
}
