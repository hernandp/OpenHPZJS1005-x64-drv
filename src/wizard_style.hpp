// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <windows.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <memory>

namespace Lj1005::WizardStyle {
    constexpr Gdiplus::ARGB SIDEBAR_GOLD = 0xffedc456;
    constexpr Gdiplus::ARGB SIDEBAR_AMBER = 0xffbc8b35;
    constexpr Gdiplus::ARGB SIDEBAR_WALNUT = 0xff4d432f;
    constexpr Gdiplus::ARGB ARTWORK_MATTE = 0xfffffdf7;
    constexpr Gdiplus::ARGB ARTWORK_FRAME = 0xffad842e;
    constexpr Gdiplus::ARGB ARTWORK_SHADOW = 0x304d432f;
    constexpr float ARTWORK_OUTER_INSET = 0.075f;
    constexpr float ARTWORK_MATTE_INSET = 0.036f;
    constexpr float ARTWORK_SHADOW_OFFSET = 0.014f;
    constexpr wchar_t HEADING_FONT_PROPERTY[] = L"BenteveoHeadingFont";

    inline bool HighContrast() {
        HIGHCONTRASTW contrast{sizeof(contrast)};
        return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(contrast), &contrast, 0) &&
               (contrast.dwFlags & HCF_HIGHCONTRASTON);
    }

    inline void StyleHeading(HWND page, int headingId) {
        const auto heading = GetDlgItem(page, headingId);
        const auto font = reinterpret_cast<HFONT>(SendMessageW(heading, WM_GETFONT, 0, 0));
        LOGFONTW description{};
        if (!GetObjectW(font, sizeof(description), &description))
            return;
        description.lfWeight = FW_BOLD;
        description.lfHeight = MulDiv(description.lfHeight, 4, 3);
        const auto title = CreateFontIndirectW(&description);
        if (title && SetPropW(page, HEADING_FONT_PROPERTY, title))
            SendMessageW(heading, WM_SETFONT, reinterpret_cast<WPARAM>(title), FALSE);
        else if (title)
            DeleteObject(title);
    }

    struct Artwork {
        ULONG_PTR token = 0;
        IStream* stream = nullptr;
        std::unique_ptr<Gdiplus::Image> image;

        Artwork(HINSTANCE instance, WORD resourceId) {
            const Gdiplus::GdiplusStartupInput input;
            if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok)
                return;
            const auto resource = FindResourceW(instance, MAKEINTRESOURCEW(resourceId), RT_RCDATA);
            if (!resource)
                return;
            stream = SHCreateMemStream(static_cast<const BYTE*>(LockResource(LoadResource(instance, resource))),
                                       SizeofResource(instance, resource));
            if (stream)
                image = std::make_unique<Gdiplus::Image>(stream);
        }

        ~Artwork() {
            image.reset();
            if (stream)
                stream->Release();
            if (token)
                Gdiplus::GdiplusShutdown(token);
        }

        void Draw(const DRAWITEMSTRUCT& item) const {
            const auto& bounds = item.rcItem;
            const bool highContrast = HighContrast();
            const Gdiplus::RectF panel(static_cast<float>(bounds.left), static_cast<float>(bounds.top),
                                       static_cast<float>(bounds.right - bounds.left),
                                       static_cast<float>(bounds.bottom - bounds.top));
            if (panel.Width <= 0 || panel.Height <= 0)
                return;
            Gdiplus::Graphics graphics(item.hDC);
            if (highContrast || !token) {
                FillRect(item.hDC, &bounds, GetSysColorBrush(COLOR_WINDOW));
                FrameRect(item.hDC, &bounds, GetSysColorBrush(COLOR_WINDOWTEXT));
            }
            else {
                const Gdiplus::Color gold(SIDEBAR_GOLD);
                const Gdiplus::Color walnut(SIDEBAR_WALNUT);
                Gdiplus::LinearGradientBrush background(panel, gold, walnut, Gdiplus::LinearGradientModeVertical);
                const Gdiplus::Color colors[]{gold, Gdiplus::Color(SIDEBAR_AMBER), walnut};
                constexpr float STOPS[]{0.0f, 0.38f, 1.0f};
                background.SetInterpolationColors(colors, STOPS, ARRAYSIZE(STOPS));
                graphics.FillRectangle(&background, panel);
            }
            if (!image || image->GetLastStatus() != Gdiplus::Ok)
                return;

            const auto outer = panel.Width * ARTWORK_OUTER_INSET;
            const auto matte = panel.Width * ARTWORK_MATTE_INSET;
            const auto width = panel.Width - 2 * (outer + matte);
            const auto height = width * static_cast<float>(image->GetHeight()) / static_cast<float>(image->GetWidth());
            const Gdiplus::RectF frame(panel.X + outer, panel.Y + outer, width + 2 * matte, height + 2 * matte);
            if (!highContrast) {
                auto shadow = frame;
                shadow.Offset(0, panel.Width * ARTWORK_SHADOW_OFFSET);
                const Gdiplus::SolidBrush shade{Gdiplus::Color(ARTWORK_SHADOW)};
                graphics.FillRectangle(&shade, shadow);
            }
            const Gdiplus::SolidBrush paper{Gdiplus::Color(ARTWORK_MATTE)};
            graphics.FillRectangle(&paper, frame);
            if (!highContrast) {
                const Gdiplus::Pen border{Gdiplus::Color(ARTWORK_FRAME)};
                graphics.DrawRectangle(&border, frame);
            }
            graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            graphics.DrawImage(image.get(), Gdiplus::RectF(frame.X + matte, frame.Y + matte, width, height));
        }
    };
}
