// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include "about.hpp"
#include "alignment_resource.h"
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

namespace Lj1005 {
    namespace {
        struct Resource {
            const BYTE* bytes = nullptr;
            DWORD size = 0;
        };

        Resource ReadResource(HINSTANCE module, int id) {
            const auto handle = FindResourceW(module, MAKEINTRESOURCEW(id), RT_RCDATA);
            if (!handle)
                return {};
            const auto data = LoadResource(module, handle);
            if (!data)
                return {};
            return {static_cast<const BYTE*>(LockResource(data)), SizeofResource(module, handle)};
        }

        struct AboutContent {
            ULONG_PTR graphicsToken = 0;
            IStream* stream = nullptr;
            std::unique_ptr<Gdiplus::Image> artwork;
            HFONT titleFont = nullptr;
            HFONT licenseFont = nullptr;

            ~AboutContent() {
                artwork.reset();
                if (stream)
                    stream->Release();
                if (graphicsToken)
                    Gdiplus::GdiplusShutdown(graphicsToken);
                if (titleFont)
                    DeleteObject(titleFont);
                if (licenseFont)
                    DeleteObject(licenseFont);
            }
        };

        void LoadArtwork(HINSTANCE module, AboutContent& content) {
            const auto image = ReadResource(module, IDR_ABOUT_ART);
            if (!image.bytes)
                return;
            const Gdiplus::GdiplusStartupInput input;
            if (Gdiplus::GdiplusStartup(&content.graphicsToken, &input, nullptr) != Gdiplus::Ok)
                return;
            content.stream = SHCreateMemStream(image.bytes, image.size);
            if (!content.stream)
                return;
            auto artwork = std::make_unique<Gdiplus::Image>(content.stream);
            if (artwork->GetLastStatus() == Gdiplus::Ok)
                content.artwork = std::move(artwork);
        }

        void ShowVersion(HWND dialog, HINSTANCE module) {
            wchar_t path[MAX_PATH]{};
            const auto length = GetModuleFileNameW(module, path, ARRAYSIZE(path));
            if (!length || length >= ARRAYSIZE(path))
                return;
            const auto size = GetFileVersionInfoSizeW(path, nullptr);
            if (!size)
                return;
            std::vector<BYTE> data(size);
            if (!GetFileVersionInfoW(path, 0, size, data.data()))
                return;

            void* value = nullptr;
            UINT valueSize = 0;
            if (VerQueryValueW(data.data(), L"\\", &value, &valueSize) && valueSize >= sizeof(VS_FIXEDFILEINFO)) {
                const auto& version = *static_cast<const VS_FIXEDFILEINFO*>(value);
                auto label = L"Version " + std::to_wstring(HIWORD(version.dwProductVersionMS)) + L"." +
                             std::to_wstring(LOWORD(version.dwProductVersionMS)) + L"." +
                             std::to_wstring(HIWORD(version.dwProductVersionLS));
                if (const auto build = LOWORD(version.dwProductVersionLS))
                    label += L"." + std::to_wstring(build);
                SetDlgItemTextW(dialog, IDC_ABOUT_VERSION, label.c_str());
            }
            if (VerQueryValueW(data.data(), L"\\StringFileInfo\\040904b0\\LegalCopyright", &value, &valueSize))
                SetDlgItemTextW(dialog, IDC_ABOUT_COPYRIGHT, static_cast<const wchar_t*>(value));
        }

        void ShowLicense(HWND dialog, HINSTANCE module) {
            const auto license = ReadResource(module, IDR_ABOUT_LICENSE);
            if (!license.bytes)
                return;
            const auto* bytes = reinterpret_cast<const char*>(license.bytes);
            const auto size = static_cast<int>(license.size);
            const auto count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, nullptr, 0);
            if (!count)
                return;
            std::wstring decoded(count, L'\0');
            if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes, size, decoded.data(), count))
                return;
            std::wstring text;
            for (const auto ch : decoded) {
                if (ch == L'\n')
                    text += L'\r';
                if (ch != L'\r')
                    text += ch;
            }
            SetDlgItemTextW(dialog, IDC_ABOUT_LICENSE, text.c_str());
            const int tabWidth = 4; // Edit control tab stops are in dialog units.
            const auto edit = GetDlgItem(dialog, IDC_ABOUT_LICENSE);
            SendMessageW(edit, EM_SETTABSTOPS, 1, reinterpret_cast<LPARAM>(&tabWidth));
        }

        void LayoutAboutPage(HWND dialog) {
            RECT client{};
            if (!GetClientRect(dialog, &client))
                return;
            RECT padding{0, 0, 8, 8};
            MapDialogRect(dialog, &padding);
            for (const auto id : {IDC_ABOUT_TITLE, IDC_ABOUT_DESCRIPTION, IDC_ABOUT_VERSION, IDC_ABOUT_COPYRIGHT,
                                  IDC_ABOUT_LICENSE_LABEL, IDC_ABOUT_LICENSE}) {
                const auto control = GetDlgItem(dialog, id);
                RECT bounds{};
                if (!GetWindowRect(control, &bounds))
                    continue;
                MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&bounds), 2);
                const auto width = std::max<LONG>(0, client.right - bounds.left - padding.right);
                const auto height = id == IDC_ABOUT_LICENSE
                                        ? std::max<LONG>(0, client.bottom - bounds.top - padding.bottom)
                                        : bounds.bottom - bounds.top;
                SetWindowPos(control, nullptr, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }

        void DrawArtwork(const DRAWITEMSTRUCT& item, const AboutContent& content) {
            FillRect(item.hDC, &item.rcItem, GetSysColorBrush(COLOR_BTNFACE));
            if (!content.artwork)
                return;
            auto& image = *content.artwork;
            const auto width = static_cast<Gdiplus::REAL>(item.rcItem.right - item.rcItem.left);
            const auto height = static_cast<Gdiplus::REAL>(item.rcItem.bottom - item.rcItem.top);
            const auto imageWidth = static_cast<Gdiplus::REAL>(image.GetWidth());
            const auto imageHeight = static_cast<Gdiplus::REAL>(image.GetHeight());
            if (imageWidth <= 0 || imageHeight <= 0)
                return;
            const auto scale = std::min(width / imageWidth, height / imageHeight);
            const auto drawWidth = scale * imageWidth, drawHeight = scale * imageHeight;
            Gdiplus::Graphics graphics(item.hDC);
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(&image, static_cast<Gdiplus::REAL>(item.rcItem.left) + (width - drawWidth) / 2,
                               static_cast<Gdiplus::REAL>(item.rcItem.top) + (height - drawHeight) / 2, drawWidth,
                               drawHeight);
        }

        INT_PTR CALLBACK AboutDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) try {
            if (message == WM_INITDIALOG) {
                const auto module = reinterpret_cast<const PROPSHEETPAGEW*>(lParam)->hInstance;
                auto content = std::make_unique<AboutContent>();
                const auto title = GetDlgItem(dialog, IDC_ABOUT_TITLE);
                const auto font = reinterpret_cast<HFONT>(SendMessageW(title, WM_GETFONT, 0, 0));
                LOGFONTW normal{};
                if (font && GetObjectW(font, sizeof(normal), &normal)) {
                    auto bold = normal;
                    bold.lfWeight = FW_BOLD;
                    content->titleFont = CreateFontIndirectW(&bold);
                    if (content->titleFont)
                        SendMessageW(title, WM_SETFONT, reinterpret_cast<WPARAM>(content->titleFont), FALSE);
                    normal.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
                    wcscpy_s(normal.lfFaceName, L"Courier New");
                    content->licenseFont = CreateFontIndirectW(&normal);
                    if (content->licenseFont)
                        SendDlgItemMessageW(dialog, IDC_ABOUT_LICENSE, WM_SETFONT,
                                            reinterpret_cast<WPARAM>(content->licenseFont), FALSE);
                }
                ShowVersion(dialog, module);
                ShowLicense(dialog, module);
                LoadArtwork(module, *content);
                SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(content.release()));
                LayoutAboutPage(dialog);
                return TRUE;
            }
            if (message == WM_SIZE) {
                LayoutAboutPage(dialog);
                return TRUE;
            }
            if (message == WM_NOTIFY && reinterpret_cast<const NMHDR*>(lParam)->code == PSN_SETACTIVE) {
                // Let the property sheet finish assigning focus before clearing its automatic selection.
                PostMessageW(GetDlgItem(dialog, IDC_ABOUT_LICENSE), EM_SETSEL, 0, 0);
            }
            auto* content = reinterpret_cast<AboutContent*>(GetWindowLongPtrW(dialog, DWLP_USER));
            if (message == WM_DRAWITEM && wParam == IDC_ABOUT_ART && content) {
                DrawArtwork(*reinterpret_cast<const DRAWITEMSTRUCT*>(lParam), *content);
                return TRUE;
            }
            if (message == WM_NCDESTROY)
                delete content;
            return FALSE;
        }

        catch (...) {
            return FALSE;
        }
    }

    HPROPSHEETPAGE CreateAboutPage(HINSTANCE module) {
        PROPSHEETPAGEW page{};
        page.dwSize = sizeof(page);
        page.hInstance = module;
        page.pszTemplate = MAKEINTRESOURCEW(IDD_ABOUT);
        page.pfnDlgProc = AboutDialog;
        return CreatePropertySheetPageW(&page);
    }
}
