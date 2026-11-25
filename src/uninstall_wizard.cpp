// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "uninstall.hpp"
#include "uninstall_wizard.h"
#include "wizard_style.hpp"
#include <commctrl.h>
#include <array>
#include <optional>
#include <stdexcept>
#include <system_error>
#include <thread>

namespace Lj1005::Uninstall {
    namespace {
        namespace Fs = std::filesystem;

        constexpr UINT WORK_COMPLETE = WM_APP + 1;
        constexpr UINT WORK_PROGRESS = WM_APP + 2;
        constexpr DWORD PATH_CAPACITY = 32768;
        constexpr wchar_t TITLE[] = L"Project Benteveo Uninstall Wizard";

        enum class Work {
            inspect,
            remove,
            trust
        };

        std::wstring Widen(const char* text) {
            const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
            const auto encoding = length ? CP_UTF8 : CP_ACP;
            const auto size = length ? length : MultiByteToWideChar(encoding, 0, text, -1, nullptr, 0);
            if (!size)
                return L"The operation failed.";
            std::wstring result(static_cast<std::size_t>(size), L'\0');
            MultiByteToWideChar(encoding, 0, text, -1, result.data(), size);
            result.pop_back();
            return result;
        }

        Fs::path ProgramFolder() {
            std::wstring buffer(PATH_CAPACITY, L'\0');
            const auto length = GetModuleFileNameW(nullptr, buffer.data(), PATH_CAPACITY);
            if (!length || length >= PATH_CAPACITY)
                throw std::runtime_error("Could not find the extracted driver folder.");
            buffer.resize(length);
            return Fs::path(buffer).parent_path();
        }

        std::wstring Describe(const State& state) {
            std::wstring text = L"Reference version " + state.version + L"\r\n";
            for (const auto& queue : state.queues) {
                text += L"\r\nQueue: " + queue.name + L" (" + queue.port + L")";
                if (queue.jobs)
                    text += L" — pending jobs: " + std::to_wstring(queue.jobs);
            }
            if (state.driver)
                text += L"\r\nPrinter driver: HP Laserjet 1005 (Project Benteveo Driver)";
            if (!state.publishedInf.empty())
                text += L"\r\nDriver Store package: " + state.publishedInf;
            if (state.monitor)
                text += L"\r\nBenteveo firmware monitor registration";
            for (const auto& file : state.files)
                text += L"\r\n" + file.native();
            if (!state.HasDriverComponents())
                text += L"\r\nThe Benteveo driver, queues, monitor and installed files are absent.";
            text += state.testerTrust ? L"\r\n\r\nTester trust is present." : L"\r\n\r\nTester trust is absent.";
            return text;
        }

        struct Result {
            std::optional<State> state;
            std::wstring error;
            bool restart = false;
        };

        struct Wizard {
            const Fs::path package = ProgramFolder();
            WizardStyle::Artwork artwork;
            std::optional<State> state;
            std::thread worker;
            Result result;
            Work action = Work::inspect;
            bool started = false;
            bool completed = false;

            explicit Wizard(HINSTANCE instance) : artwork(instance, IDR_WIZARD_ART) {}

            ~Wizard() {
                if (worker.joinable())
                    worker.join();
            }

            void Start(HWND page, Work operation) {
                if (worker.joinable())
                    return;
                action = operation;
                started = true;
                if (operation != Work::trust)
                    completed = false;
                SetDlgItemTextW(page, IDC_STATUS,
                                operation == Work::trust ? L"Checking certificate dependencies..."
                                                         : L"Checking Windows printing components...");
                PropSheet_SetWizButtons(GetParent(page), PSWIZB_DISABLEDFINISH);
                EnableWindow(GetDlgItem(GetParent(page), IDCANCEL), FALSE);
                for (const int id : {IDC_CHECK, IDC_CERTIFICATE})
                    EnableWindow(GetDlgItem(page, id), FALSE);
                worker = std::thread([this, page, operation] {
                    Result outcome;
                    try {
                        if (operation == Work::inspect)
                            outcome.state = Inspect(package);
                        else if (operation == Work::remove)
                            outcome.state = Remove(package, state.value(), [page](const wchar_t* step) {
                                PostMessageW(page, WORK_PROGRESS, 0, reinterpret_cast<LPARAM>(step));
                            });
                        else
                            outcome.state = RemoveTrust(package);
                    }
                    catch (const std::system_error& error) {
                        const auto code = error.code().value();
                        outcome.restart =
                            operation == Work::remove &&
                            (code == ERROR_PRINT_MONITOR_IN_USE || code == ERROR_PRINTER_DRIVER_IN_USE ||
                             code == ERROR_PRINTER_DRIVER_PACKAGE_IN_USE || code == ERROR_SHARING_VIOLATION ||
                             code == ERROR_BUSY || code == ERROR_SUCCESS_REBOOT_REQUIRED ||
                             code == ERROR_SUCCESS_RESTART_REQUIRED);
                        outcome.error = Widen(error.what()) + L" (Windows error " + std::to_wstring(code) + L").";
                    }
                    catch (const std::exception& error) {
                        outcome.error = Widen(error.what());
                    }
                    if (!outcome.error.empty() && operation != Work::inspect) {
                        try {
                            outcome.state = Inspect(package);
                        }
                        catch (const std::exception& error) {
                            outcome.error += L"\r\nCould not verify remaining components. " + Widen(error.what());
                        }
                    }
                    result = std::move(outcome);
                    PostMessageW(page, WORK_COMPLETE, 0, 0);
                });
            }

            void Update(HWND page) const {
                const HWND sheet = GetParent(page);
                const bool canRemove =
                    state && state->HasDriverComponents() && !state->HasPendingJobs() && result.error.empty();
                PropSheet_SetFinishText(sheet, canRemove ? L"&Remove" : L"E&xit");
                PropSheet_SetWizButtons(sheet, PSWIZB_BACK | PSWIZB_FINISH);
                EnableWindow(GetDlgItem(sheet, IDCANCEL), TRUE);
                const bool certificate = completed && state && state->testerTrust;
                ShowWindow(GetDlgItem(page, IDC_CERTIFICATE), certificate ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(page, IDC_CHECK), completed ? SW_HIDE : SW_SHOW);
                for (const int id : {IDC_CHECK, IDC_CERTIFICATE})
                    EnableWindow(GetDlgItem(page, id), TRUE);
                SetDlgItemTextW(page, IDC_HEADING, completed ? L"Removal complete" : L"Review removal");

                const wchar_t* status = L"Checking the extracted driver package.";
                if (result.restart)
                    status = L"Restart Windows and reopen the wizard to finish cleanup.";
                else if (!result.error.empty())
                    status =
                        action == Work::trust ? L"Certificate cleanup needs attention." : L"Removal needs attention.";
                else if (completed)
                    status = L"The driver cleanup check passed.";
                else if (state && state->HasPendingJobs())
                    status = L"Finish or cancel the listed print jobs, then check again.";
                else if (canRemove)
                    status = L"Click Remove to remove the listed components.";
                SetDlgItemTextW(page, IDC_STATUS, status);

                std::wstring details = L"Package: " + package.native() + L"\r\n";
                if (!result.error.empty())
                    details += L"\r\n" + result.error + L"\r\n";
                if (state)
                    details += L"\r\n" + Describe(*state);
                SetDlgItemTextW(page, IDC_DETAILS, details.c_str());
            }
        };

        INT_PTR CALLBACK Page(HWND page, UINT message, WPARAM parameter, LPARAM data) {
            if (message == WM_INITDIALOG) {
                const auto* info = reinterpret_cast<const PROPSHEETPAGEW*>(data);
                SetWindowLongPtrW(page, DWLP_USER, info->lParam);
                WizardStyle::StyleHeading(page, IDC_HEADING);
                ShowWindow(GetDlgItem(page, IDC_CERTIFICATE), SW_HIDE);
                return TRUE;
            }
            auto* wizard = reinterpret_cast<Wizard*>(GetWindowLongPtrW(page, DWLP_USER));
            if (!wizard)
                return FALSE;
            switch (message) {
            case WM_NOTIFY: {
                const auto* notification = reinterpret_cast<const NMHDR*>(data);
                if (notification->code == PSN_SETACTIVE) {
                    if (!GetDlgItem(page, IDC_DETAILS))
                        PropSheet_SetWizButtons(GetParent(page), PSWIZB_NEXT);
                    else if (!wizard->started)
                        wizard->Start(page, Work::inspect);
                    else if (!wizard->worker.joinable())
                        wizard->Update(page);
                    return TRUE;
                }
                if (notification->code == PSN_WIZFINISH) {
                    if (wizard->worker.joinable()) {
                        SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);
                        return TRUE;
                    }
                    if (wizard->state && wizard->state->HasDriverComponents() && !wizard->state->HasPendingJobs() &&
                        wizard->result.error.empty()) {
                        wizard->Start(page, Work::remove);
                        SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);
                        return TRUE;
                    }
                }
                if (notification->code == PSN_QUERYCANCEL && wizard->worker.joinable()) {
                    SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
                return FALSE;
            }
            case WM_COMMAND:
                if (wizard->worker.joinable())
                    return FALSE;
                switch (LOWORD(parameter)) {
                case IDC_CHECK:
                    wizard->Start(page, Work::inspect);
                    return TRUE;
                case IDC_CERTIFICATE:
                    if (MessageBoxW(
                            page,
                            L"Remove the Benteveo tester certificate from the local computer's Trusted Root and Trusted Publishers stores?",
                            TITLE, MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2) == IDYES)
                        wizard->Start(page, Work::trust);
                    return TRUE;
                default:
                    return FALSE;
                }
            case WORK_PROGRESS:
                SetDlgItemTextW(page, IDC_STATUS, reinterpret_cast<const wchar_t*>(data));
                return TRUE;
            case WORK_COMPLETE:
                wizard->worker.join();
                wizard->state = wizard->result.state;
                wizard->completed = wizard->state && !wizard->state->HasDriverComponents() &&
                                    (wizard->result.error.empty() || wizard->action == Work::trust);
                wizard->Update(page);
                return TRUE;
            case WM_DRAWITEM:
                if (parameter == IDC_ARTWORK) {
                    wizard->artwork.Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(data));
                    return TRUE;
                }
                return FALSE;
            case WM_DESTROY:
                if (const auto font = RemovePropW(page, WizardStyle::HEADING_FONT_PROPERTY))
                    DeleteObject(font);
                return TRUE;
            default:
                return FALSE;
            }
        }

        int Run(HINSTANCE instance) {
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_WIN95_CLASSES};
            if (!InitCommonControlsEx(&controls))
                return 1;
            Wizard wizard(instance);
            constexpr std::array TEMPLATES{IDD_WELCOME, IDD_REVIEW};
            std::array<PROPSHEETPAGEW, TEMPLATES.size()> pages{};
            for (std::size_t index = 0; index < pages.size(); ++index) {
                auto& page = pages[index];
                page.dwSize = sizeof(page);
                page.dwFlags = PSP_HIDEHEADER;
                page.hInstance = instance;
                page.pszTemplate = MAKEINTRESOURCEW(TEMPLATES[index]);
                page.pfnDlgProc = Page;
                page.lParam = reinterpret_cast<LPARAM>(&wizard);
            }
            PROPSHEETHEADERW sheet{};
            sheet.dwSize = sizeof(sheet);
            sheet.dwFlags = PSH_WIZARD97 | PSH_PROPSHEETPAGE;
            sheet.hInstance = instance;
            sheet.pszCaption = TITLE;
            sheet.nPages = static_cast<UINT>(pages.size());
            sheet.ppsp = pages.data();
            return PropertySheetW(&sheet) == -1 ? 1 : 0;
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    try {
        return Lj1005::Uninstall::Run(instance);
    }
    catch (const std::exception& error) {
        using namespace Lj1005::Uninstall;
        MessageBoxW(nullptr, Widen(error.what()).c_str(), TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
}
