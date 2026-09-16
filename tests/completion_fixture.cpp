// SPDX-License-Identifier: GPL-2.0-or-later
// Linked only into the test fixture DLL, never the installed monitor.
#include "../driver/completion_backend.hpp"
#include <stdexcept>

namespace {
    DWORD acknowledgements = 0, scenario = 0;

    class Fixture final : public lj1005::CompletionBackend {
        HANDLE handle_;
        MONITOR2 lower_;

    public:
        Fixture(HANDLE handle, const MONITOR2& lower) : handle_(handle), lower_(lower) {}

        lj1005::Bytes Read() override {
            if (scenario == 2)
                throw std::runtime_error("Simulated status-read failure");
            lj1005::Bytes bytes(4096);
            DWORD count = 0;
            if (!lower_.pfnReadPort ||
                !lower_.pfnReadPort(handle_, bytes.data(), static_cast<DWORD>(bytes.size()), &count))
                throw std::runtime_error("Fixture read failed");
            bytes.resize(count);
            return bytes;
        }

        bool IsCancelled() override {
            return scenario == 1;
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
}

namespace lj1005 {
    std::unique_ptr<CompletionBackend> CreateCompletionBackend(std::wstring_view, HANDLE handle, const MONITOR2& lower,
                                                               LPWSTR, DWORD) {
        return std::make_unique<Fixture>(handle, lower);
    }
}
