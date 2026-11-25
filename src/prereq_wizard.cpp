// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "prereq_wizard.h"
#include "prerequisites.hpp"
#include "wizard_style.hpp"
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shellapi.h>
#include <array>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <thread>

namespace Lj1005::Prerequisites {
    namespace {
        namespace Fs = std::filesystem;

        constexpr UINT WORK_COMPLETE = WM_APP + 1;
        constexpr UINT DOWNLOAD_PROGRESS = WM_APP + 2;
        constexpr DWORD PRINTER_WINDOW_TIMEOUT_MS = 5000;
        constexpr DWORD PRINTER_WINDOW_POLL_MS = 50;
        constexpr std::size_t BYTES_PER_KIB = 1024;
        constexpr DWORD UTILITY_PATH_CAPACITY = 32768;
        constexpr DWORD CERTIFICATE_FAILED = ERROR_INSTALL_FAILURE;
        constexpr wchar_t ELEVATED_ACTION[] = L"--install-tester-certificate";
        constexpr wchar_t TITLE[] = L"Project Benteveo Prerequisites Installation Tool";
        constexpr COLORREF READY_COLOR = RGB(0, 109, 50);
        constexpr COLORREF INCOMPLETE_COLOR = RGB(181, 23, 35);
        constexpr std::array RESULT_IDS{IDC_CERT_RESULT, IDC_FW_RESULT, IDC_USB_RESULT};
        constexpr std::array MARK_IDS{IDC_CERT_MARK, IDC_FW_MARK, IDC_USB_MARK};
        constexpr std::array LABELS{L"Certificates", L"HP firmware", L"USB connection"};

        enum class Work {
            inspect,
            certificates,
            download,
            browse,
            openPrinter
        };

        std::wstring Message(const char* message) {
            const auto length = MultiByteToWideChar(CP_UTF8, 0, message, -1, nullptr, 0);
            if (!length)
                return L"The operation failed.";
            std::wstring result(static_cast<std::size_t>(length), L'\0');
            MultiByteToWideChar(CP_UTF8, 0, message, -1, result.data(), length);
            result.pop_back();
            return result;
        }

        Fs::path ProgramPath() {
            std::wstring path(UTILITY_PATH_CAPACITY, L'\0');
            const auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
            if (!length || length >= path.size())
                throw std::runtime_error("Could not find the extracted driver folder.");
            path.resize(length);
            return path;
        }

        Fs::path PickFirmware(HWND owner) {
            wchar_t name[UTILITY_PATH_CAPACITY]{};
            OPENFILENAMEW file{};
            file.lStructSize = sizeof(file);
            file.hwndOwner = owner;
            file.lpstrFilter = L"Original HP firmware (sihp1005.img)\0sihp1005.img\0Firmware images (*.img)\0*.img\0";
            file.lpstrFile = name;
            file.nMaxFile = ARRAYSIZE(name);
            file.lpstrTitle = L"Choose the original HP firmware image";
            file.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
            return GetOpenFileNameW(&file) ? Fs::path(name) : Fs::path{};
        }

        struct PrinterDialog {
            DWORD processId = 0;
            HWND window = nullptr;

            void Activate(HWND owner) const {
                DWORD currentProcessId = 0;
                if (GetForegroundWindow() != owner || !GetWindowThreadProcessId(window, &currentProcessId) ||
                    currentProcessId != processId)
                    return;
                if (!SetForegroundWindow(window))
                    FlashWindow(window, TRUE);
            }
        };

        BOOL CALLBACK FindPrinterWindow(HWND window, LPARAM data) {
            auto& dialog = *reinterpret_cast<PrinterDialog*>(data);
            DWORD processId = 0;
            GetWindowThreadProcessId(window, &processId);
            if (processId != dialog.processId || !IsWindowVisible(window) ||
                IsWindowVisible(GetWindow(window, GW_OWNER)))
                return TRUE;
            dialog.window = window;
            return FALSE;
        }

        PrinterDialog OpenPrinter(HWND owner, std::stop_token stop) {
            wchar_t system[MAX_PATH]{};
            const auto length = GetSystemDirectoryW(system, ARRAYSIZE(system));
            if (!length || length >= ARRAYSIZE(system))
                throw std::runtime_error("Could not locate the Windows Add Printer Wizard.");
            const auto program = Fs::path(system) / L"rundll32.exe";
            const auto dll = Fs::path(system) / L"printui.dll";
            auto command = L"\"" + program.wstring() + L"\" \"" + dll.wstring() + L"\",PrintUIEntry /im";
            STARTUPINFOW startup{sizeof(startup)};
            PROCESS_INFORMATION child{};
            if (!CreateProcessW(program.c_str(), command.data(), nullptr, nullptr, FALSE, 0, nullptr, system, &startup,
                                &child))
                throw std::runtime_error(
                    "Could not open Add Printer. Use Printers & scanners to add the printer manually.");
            CloseHandle(child.hThread);
            const std::unique_ptr<void, decltype(&CloseHandle)> process(child.hProcess, CloseHandle);
            if (GetForegroundWindow() == owner)
                AllowSetForegroundWindow(child.dwProcessId);

            // Keep our wizard alive until we have a visible window to hand focus to.
            PrinterDialog dialog{child.dwProcessId};
            const auto deadline = GetTickCount64() + PRINTER_WINDOW_TIMEOUT_MS;
            while (!stop.stop_requested() && GetTickCount64() < deadline) {
                EnumWindows(FindPrinterWindow, reinterpret_cast<LPARAM>(&dialog));
                if (dialog.window)
                    return dialog;
                if (WaitForSingleObject(process.get(), PRINTER_WINDOW_POLL_MS) != WAIT_TIMEOUT)
                    break;
            }
            if (stop.stop_requested())
                return {};
            throw std::runtime_error(
                "Could not find the Add Printer window. Check the taskbar or open Printers & scanners.");
        }

        struct Wizard {
            Fs::path program;
            Fs::path directory;
            WizardStyle::Artwork artwork;
            Status status;
            Status result;
            std::wstring detail;
            std::wstring error;
            std::jthread worker;
            Work work = Work::inspect;
            bool closeRequested = false;
            PrinterDialog printer;

            Wizard(HINSTANCE instance, Fs::path executable)
                : program(std::move(executable)), directory(program.parent_path()), artwork(instance, IDR_WIZARD_ART) {}

            void Render(HWND page, bool prepare) const {
                const bool busy = worker.joinable();
                const std::array checks{status.certificate, status.firmware, status.usb};
                for (std::size_t index = 0; index < checks.size(); ++index) {
                    const auto& check = checks[index];
                    const bool known = !check.detail.empty();
                    SetDlgItemTextW(page, MARK_IDS[index], known ? (check.ready ? L"\u2713" : L"\u2717") : L"");
                    const auto text = std::wstring(LABELS[index]) + L": " +
                                      (known ? (check.ready ? L"Ready. " : L"Needs attention. ") : L"Checking...") +
                                      check.detail;
                    SetDlgItemTextW(page, RESULT_IDS[index], text.c_str());
                    InvalidateRect(GetDlgItem(page, MARK_IDS[index]), nullptr, TRUE);
                }

                const bool ready = !busy && status.Ready();
                SetDlgItemTextW(page, IDC_HEADING,
                                ready ? L"Ready for Have Disk"
                                      : (prepare ? L"Prepare for Have Disk" : L"Check prerequisites"));
                const auto inf = (directory / INF_NAME).wstring();
                SetDlgItemTextW(page, IDC_INF_PATH, inf.c_str());
                ShowWindow(GetDlgItem(page, IDC_INF_PATH), ready ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(page, IDC_OPEN_PRINTER), ready ? SW_SHOW : SW_HIDE);
                ShowWindow(GetDlgItem(page, IDC_ACTION), !busy && !ready ? SW_SHOW : SW_HIDE);
                if (prepare)
                    ShowWindow(GetDlgItem(page, IDC_BROWSE),
                               !busy && status.certificate.ready && !status.firmware.ready ? SW_SHOW : SW_HIDE);

                std::wstring message = detail;
                if (ready && detail.empty())
                    message = L"Choose Add a local printer or network printer with manual settings. Then select " +
                              status.port + L" as the existing port and browse to the INF below.";
                else if (message.empty() && !prepare)
                    message = L"Click Next to prepare the missing prerequisites.";
                SetDlgItemTextW(page, IDC_MESSAGE, message.c_str());

                auto action = L"Check again";
                if (prepare) {
                    if (!status.certificate.ready && !status.missingStores.empty())
                        action = L"Install certificates";
                    else if (status.certificate.ready && !status.firmware.ready)
                        action = L"Download firmware";
                    else
                        action = L"Retry";
                }
                SetDlgItemTextW(page, IDC_ACTION, action);
                const auto sheet = GetParent(page);
                if (ready) {
                    PropSheet_SetFinishText(sheet, L"Exit");
                    PropSheet_SetWizButtons(sheet, PSWIZB_FINISH | PSWIZB_BACK);
                }
                else {
                    PropSheet_SetWizButtons(sheet, busy ? 0 : PSWIZB_BACK | (prepare ? 0 : PSWIZB_NEXT));
                }
            }

            void Start(HWND page, bool prepare, Work action, Fs::path file = {}, HANDLE process = nullptr) {
                work = action;
                error.clear();
                printer = {};
                detail = action == Work::download       ? L"Downloading the original HP firmware..."
                         : action == Work::certificates ? L"Waiting for Windows to add tester trust..."
                         : action == Work::browse       ? L"Verifying the selected firmware..."
                                                        : L"Checking prerequisites...";
                const auto sheet = GetParent(page);
                worker =
                    std::jthread([this, page, sheet, action, image = std::move(file), process](std::stop_token stop) {
                        try {
                            if (action == Work::download) {
                                DownloadFirmware(directory, stop, [page](std::size_t bytes) {
                                    PostMessageW(page, DOWNLOAD_PROGRESS, bytes / BYTES_PER_KIB, 0);
                                });
                            }
                            else if (action == Work::browse) {
                                SupplyFirmware(directory, image);
                            }
                            else if (action == Work::certificates) {
                                DWORD outcome = ERROR_GEN_FAILURE;
                                // Let the elevated operation settle before allowing the wizard to close.
                                if (WaitForSingleObject(process, INFINITE) == WAIT_OBJECT_0)
                                    GetExitCodeProcess(process, &outcome);
                                CloseHandle(process);
                                if (outcome != ERROR_SUCCESS)
                                    throw std::runtime_error("Windows could not add tester trust (error " +
                                                             std::to_string(outcome) +
                                                             "). Check the certificate policy and retry.");
                            }
                        }
                        catch (const std::exception& failure) {
                            error = Message(failure.what());
                        }
                        if (!stop.stop_requested()) {
                            result = Inspect(directory);
                            if (action == Work::openPrinter && result.Ready() && !stop.stop_requested()) {
                                try {
                                    printer = OpenPrinter(sheet, stop);
                                }
                                catch (const std::exception& failure) {
                                    error = Message(failure.what());
                                }
                            }
                        }
                        PostMessageW(page, WORK_COMPLETE, 0, 0);
                    });
                Render(page, prepare);
            }

            void InstallCertificates(HWND page) {
                std::wstring prompt = L"Add the Benteveo tester certificate to these local computer stores?\n\n";
                for (const auto& store : status.missingStores)
                    prompt += store == L"Root" ? L"Trusted Root Certification Authorities\n" : L"Trusted Publishers\n";
                prompt += L"\nOpenZJS Technical Tester Signing\nThumbprint: ";
                prompt += CERTIFICATE_THUMBPRINT;
                if (MessageBoxW(GetParent(page), prompt.c_str(), TITLE, MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) !=
                    IDYES)
                    return;
                SHELLEXECUTEINFOW child{};
                child.cbSize = sizeof(child);
                child.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
                child.hwnd = GetParent(page);
                child.lpVerb = L"runas";
                child.lpFile = program.c_str();
                child.lpParameters = ELEVATED_ACTION;
                child.nShow = SW_HIDE;
                if (!ShellExecuteExW(&child) || !child.hProcess) {
                    detail = GetLastError() == ERROR_CANCELLED
                                 ? L"Administrator permission was declined. Retry to continue."
                                 : L"Could not request administrator permission. Retry.";
                    Render(page, true);
                    return;
                }
                Start(page, true, Work::certificates, {}, child.hProcess);
            }

            void Prepare(HWND page) {
                if (!status.certificate.ready && !status.missingStores.empty())
                    InstallCertificates(page);
                else if (status.certificate.ready && !status.firmware.ready)
                    Start(page, true, Work::download);
                else
                    Start(page, true, Work::inspect);
            }

            void Complete(HWND page, bool prepare) {
                worker.join();
                if (closeRequested) {
                    PropSheet_PressButton(GetParent(page), PSBTN_CANCEL);
                    return;
                }
                const auto next = work;
                status = std::move(result);
                detail = std::move(error);
                if (printer.window) {
                    const auto sheet = GetParent(page);
                    printer.Activate(sheet);
                    PropSheet_PressButton(sheet, PSBTN_CANCEL);
                    return;
                }
                if (next == Work::openPrinter && detail.empty()) {
                    detail = L"Readiness changed. Check the results above before opening Add Printer.";
                }
                if (prepare && next == Work::certificates && detail.empty() && status.certificate.ready &&
                    !status.firmware.ready) {
                    Start(page, true, Work::download);
                    return;
                }
                if (prepare && detail.empty() && !status.Ready())
                    detail = L"Resolve the status above and retry.";
                Render(page, prepare);
            }
        };

        INT_PTR CALLBACK PageProc(HWND page, UINT message, WPARAM wParam, LPARAM lParam) {
            if (message == WM_INITDIALOG) {
                const auto* definition = reinterpret_cast<const PROPSHEETPAGEW*>(lParam);
                SetWindowLongPtrW(page, GWLP_USERDATA, definition->lParam);
                WizardStyle::StyleHeading(page, IDC_HEADING);
                return TRUE;
            }
            auto* wizard = reinterpret_cast<Wizard*>(GetWindowLongPtrW(page, GWLP_USERDATA));
            if (!wizard)
                return FALSE;
            const bool welcome = GetDlgItem(page, IDC_CERT_RESULT) == nullptr;
            const bool prepare = GetDlgItem(page, IDC_BROWSE) != nullptr;
            switch (message) {
            case WM_NOTIFY: {
                const auto* notification = reinterpret_cast<const NMHDR*>(lParam);
                if (notification->code == PSN_SETACTIVE) {
                    SetWindowTextW(GetParent(page), TITLE);
                    if (welcome) {
                        PropSheet_SetWizButtons(GetParent(page), PSWIZB_NEXT);
                    }
                    else if (!prepare) {
                        wizard->Start(page, false, Work::inspect);
                    }
                    else {
                        wizard->detail.clear();
                        wizard->Render(page, true);
                        wizard->Prepare(page);
                    }
                }
                else if (notification->code == PSN_QUERYCANCEL && wizard->worker.joinable()) {
                    wizard->closeRequested = true;
                    wizard->worker.request_stop();
                    SetDlgItemTextW(page, IDC_MESSAGE, L"Canceling when the current operation finishes...");
                    SetWindowLongPtrW(page, DWLP_MSGRESULT, TRUE);
                    return TRUE;
                }
                break;
            }
            case WM_COMMAND:
                if (wizard->worker.joinable())
                    return FALSE;
                switch (LOWORD(wParam)) {
                case IDC_ACTION:
                    if (prepare)
                        wizard->Prepare(page);
                    else
                        wizard->Start(page, false, Work::inspect);
                    return TRUE;
                case IDC_BROWSE:
                    if (const auto path = PickFirmware(GetParent(page)); !path.empty())
                        wizard->Start(page, true, Work::browse, path);
                    return TRUE;
                case IDC_OPEN_PRINTER:
                    wizard->Start(page, prepare, Work::openPrinter);
                    return TRUE;
                }
                break;
            case WM_DRAWITEM:
                if (wParam == IDC_ARTWORK) {
                    wizard->artwork.Draw(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam));
                    return TRUE;
                }
                break;
            case WM_DESTROY:
                if (const auto font = RemovePropW(page, WizardStyle::HEADING_FONT_PROPERTY))
                    DeleteObject(font);
                break;
            case WM_CTLCOLORSTATIC: {
                const auto id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
                if (id >= IDC_CERT_MARK && id <= IDC_USB_MARK) {
                    const std::array ready{wizard->status.certificate.ready, wizard->status.firmware.ready,
                                           wizard->status.usb.ready};
                    const auto dc = reinterpret_cast<HDC>(wParam);
                    SetTextColor(dc, WizardStyle::HighContrast()
                                         ? GetSysColor(COLOR_WINDOWTEXT)
                                         : (ready[static_cast<std::size_t>(id - IDC_CERT_MARK)] ? READY_COLOR
                                                                                                : INCOMPLETE_COLOR));
                    SetBkMode(dc, TRANSPARENT);
                    return reinterpret_cast<INT_PTR>(GetSysColorBrush(COLOR_3DFACE));
                }
                break;
            }
            case WORK_COMPLETE:
                wizard->Complete(page, prepare);
                return TRUE;
            case DOWNLOAD_PROGRESS:
                if (!wizard->closeRequested) {
                    const auto progress = L"Downloading from HP... " + std::to_wstring(wParam) + L" KiB";
                    SetDlgItemTextW(page, IDC_MESSAGE, progress.c_str());
                }
                return TRUE;
            }
            return FALSE;
        }

        int CALLBACK SheetProc(HWND sheet, UINT message, LPARAM) {
            if (message == PSCB_INITIALIZED)
                SetWindowTextW(sheet, TITLE);
            return 0;
        }

        int RunWizard(HINSTANCE instance, const Fs::path& program) {
            INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_WIN95_CLASSES};
            InitCommonControlsEx(&controls);
            Wizard wizard(instance, program);
            constexpr std::array TEMPLATES{IDD_WELCOME, IDD_CHECK, IDD_PREPARE};
            std::array<PROPSHEETPAGEW, TEMPLATES.size()> pages{};
            for (std::size_t index = 0; index < pages.size(); ++index) {
                auto& page = pages[index];
                page.dwSize = sizeof(page);
                page.dwFlags = PSP_HIDEHEADER;
                page.hInstance = instance;
                page.pszTemplate = MAKEINTRESOURCEW(TEMPLATES[index]);
                page.pfnDlgProc = PageProc;
                page.lParam = reinterpret_cast<LPARAM>(&wizard);
            }
            PROPSHEETHEADERW sheet{};
            sheet.dwSize = sizeof(sheet);
            sheet.dwFlags = PSH_WIZARD97 | PSH_PROPSHEETPAGE | PSH_USECALLBACK;
            sheet.hInstance = instance;
            sheet.pszCaption = TITLE;
            sheet.nPages = static_cast<UINT>(pages.size());
            sheet.ppsp = pages.data();
            sheet.pfnCallback = SheetProc;
            return PropertySheetW(&sheet) == -1 ? 1 : 0;
        }
    }
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR arguments, int) {
    using namespace Lj1005::Prerequisites;
    SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);
    const bool elevatedAction = std::wstring_view(arguments) == ELEVATED_ACTION;
    try {
        const auto program = ProgramPath();
        if (elevatedAction) {
            InstallTesterCertificate(program.parent_path());
            return ERROR_SUCCESS;
        }
        const auto result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        const auto status = RunWizard(instance, program);
        if (SUCCEEDED(result))
            CoUninitialize();
        return status;
    }
    catch (const std::system_error& error) {
        if (elevatedAction)
            return error.code().value();
        MessageBoxW(nullptr, Message(error.what()).c_str(), TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
    catch (const std::exception& error) {
        if (elevatedAction)
            return CERTIFICATE_FAILED;
        MessageBoxW(nullptr, Message(error.what()).c_str(), TITLE, MB_OK | MB_ICONERROR);
        return 1;
    }
}
