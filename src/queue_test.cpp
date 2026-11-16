// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include <windows.h>
#include <winspool.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
    constexpr int MULTI_PAGE_COUNT = 3;
    constexpr int BLANK_PAGE_INDEX = 1;
    constexpr int BORDER_INSET_PIXELS = 100;
    constexpr int BOLD_BORDER_PIXELS = 12;
    constexpr int FONT_HEIGHT_PIXELS = 100;
    constexpr int LABEL_X_PIXELS = 200;
    constexpr int LABEL_Y_PIXELS = 300;
    constexpr int BAR_COUNT = 16;
    constexpr int BAR_LEFT_PIXELS = 200;
    constexpr int BAR_PITCH_PIXELS = 200;
    constexpr int BAR_WIDTH_PIXELS = 80;
    constexpr int BAR_TOP_PIXELS = 700;
    constexpr int BAR_BOTTOM_PIXELS = 1100;
    constexpr int MARKER_LEFT_PIXELS = 200;
    constexpr int MARKER_TOP_PIXELS = 1300;
    constexpr int MARKER_BASE_RIGHT_PIXELS = 300;
    constexpr int MARKER_PAGE_STEP_PIXELS = 200;
    constexpr int MARKER_BOTTOM_PIXELS = 1500;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 3 || argc > 5) {
        std::cerr
            << "Usage: queue_test printer-name output-file|- [portrait|bold|bold-landscape|bold-multi|landscape|blank|multi|letter|emf-a4|emf-letter|emf-landscape|emf-reset] [input.emf]\n";
        return 1;
    }
    const std::wstring scenario = argc >= 4 ? argv[3] : L"portrait";
    const bool emf =
        scenario == L"emf-a4" || scenario == L"emf-letter" || scenario == L"emf-landscape" || scenario == L"emf-reset";
    if ((emf && argc != 5) || (!emf && argc == 5))
        return 1;
    if (!emf && scenario != L"portrait" && scenario != L"bold" && scenario != L"bold-landscape" &&
        scenario != L"bold-multi" && scenario != L"landscape" && scenario != L"blank" && scenario != L"multi" &&
        scenario != L"letter")
        return 1;
    const bool bold = scenario.starts_with(L"bold"), multi = scenario == L"multi" || scenario == L"bold-multi";
    HANDLE printer = nullptr;
    if (!OpenPrinterW(argv[1], &printer, nullptr)) {
        std::cerr << "OpenPrinter=" << GetLastError() << '\n';
        return 1;
    }
    const auto size = DocumentPropertiesW(nullptr, printer, argv[1], nullptr, nullptr, 0);
    if (size <= 0) {
        ClosePrinter(printer);
        std::cerr << "Cannot size DEVMODE\n";
        return 1;
    }
    std::vector<unsigned char> buffer(static_cast<std::size_t>(size));
    auto* dm = reinterpret_cast<DEVMODEW*>(buffer.data());
    if (DocumentPropertiesW(nullptr, printer, argv[1], dm, nullptr, DM_OUT_BUFFER) != IDOK) {
        ClosePrinter(printer);
        return 1;
    }
    dm->dmFields |= DM_PAPERSIZE | DM_ORIENTATION;
    dm->dmPaperSize = (scenario == L"letter" || scenario == L"emf-letter") ? DMPAPER_LETTER : DMPAPER_A4;
    dm->dmOrientation = (scenario == L"landscape" || scenario == L"bold-landscape" || scenario == L"emf-landscape")
                            ? DMORIENT_LANDSCAPE
                            : DMORIENT_PORTRAIT;
    if (DocumentPropertiesW(nullptr, printer, argv[1], dm, dm, DM_IN_BUFFER | DM_OUT_BUFFER) != IDOK) {
        ClosePrinter(printer);
        return 1;
    }
    ClosePrinter(printer);
    HDC dc = CreateDCW(L"WINSPOOL", argv[1], nullptr, dm);
    if (!dc) {
        std::cerr << "CreateDC failed: " << GetLastError() << '\n';
        return 1;
    }
    bool started = false;
    try {
        std::cout << "Raster " << GetDeviceCaps(dc, HORZRES) << 'x' << GetDeviceCaps(dc, VERTRES) << ", "
                  << GetDeviceCaps(dc, LOGPIXELSX) << " dpi\n";
        DOCINFOW doc{};
        doc.cbSize = sizeof(doc);
        doc.lpszDocName = L"Benteveo GDI integration test";
        doc.lpszOutput = std::wstring(argv[2]) == L"-" ? nullptr : argv[2];
        if (StartDocW(dc, &doc) <= 0)
            throw std::runtime_error("StartDoc failed");
        started = true;
        const int pages = (multi || scenario == L"emf-reset") ? MULTI_PAGE_COUNT : 1;
        for (int page = 0; page < pages; ++page) {
            if (scenario == L"emf-reset") {
                dm->dmPaperSize = page == BLANK_PAGE_INDEX ? DMPAPER_A4 : DMPAPER_LETTER;
                dm->dmOrientation = page == BLANK_PAGE_INDEX ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
                auto reset = ResetDCW(dc, dm);
                if (!reset)
                    throw std::runtime_error("ResetDC failed");
                dc = reset;
                std::cout << "Reset page " << page + 1 << ": " << GetDeviceCaps(dc, HORZRES) << 'x'
                          << GetDeviceCaps(dc, VERTRES) << '\n';
            }
            if (StartPage(dc) <= 0)
                throw std::runtime_error("StartPage failed");
            if (emf) {
                const auto metafile = GetEnhMetaFileW(argv[4]);
                if (!metafile)
                    throw std::runtime_error("Cannot load input EMF");
                const RECT pageBounds{0, 0, GetDeviceCaps(dc, HORZRES), GetDeviceCaps(dc, VERTRES)};
                const auto played = PlayEnhMetaFile(dc, metafile, &pageBounds);
                DeleteEnhMetaFile(metafile);
                if (!played)
                    throw std::runtime_error("EMF playback failed");
            }
            else if (scenario != L"blank" && !(multi && page == BLANK_PAGE_INDEX)) {
                const int width = GetDeviceCaps(dc, HORZRES), height = GetDeviceCaps(dc, VERTRES);
                const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                if (bold) {
                    // Half-millimetre strips, contained within the original border bounds.
                    const RECT edges[] = {{BORDER_INSET_PIXELS, BORDER_INSET_PIXELS, width - BORDER_INSET_PIXELS,
                                           (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS)},
                                          {BORDER_INSET_PIXELS, height - (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS),
                                           width - BORDER_INSET_PIXELS, height - BORDER_INSET_PIXELS},
                                          {BORDER_INSET_PIXELS, (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS),
                                           (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS),
                                           height - (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS)},
                                          {width - (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS),
                                           (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS), width - BORDER_INSET_PIXELS,
                                           height - (BORDER_INSET_PIXELS + BOLD_BORDER_PIXELS)}};
                    for (const auto& edge : edges)
                        if (!FillRect(dc, &edge, black))
                            throw std::runtime_error("Border drawing failed");
                }
                else
                    Rectangle(dc, BORDER_INSET_PIXELS, BORDER_INSET_PIXELS, width - BORDER_INSET_PIXELS,
                              height - BORDER_INSET_PIXELS);
                HFONT font =
                    CreateFontW(-FONT_HEIGHT_PIXELS, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                                OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
                auto previous = SelectObject(dc, font);
                std::wstring text = L"Benteveo - HP Laserjet 1005 driver for Windows 11 x64";
                if (bold && multi)
                    text += L" - Page " + std::to_wstring(page + 1) + L" of " + std::to_wstring(MULTI_PAGE_COUNT);
                TextOutW(dc, LABEL_X_PIXELS, LABEL_Y_PIXELS, text.c_str(), static_cast<int>(text.size()));
                SelectObject(dc, previous);
                DeleteObject(font);
                for (int i = 0; i < BAR_COUNT; ++i) {
                    if (bold) {
                        const RECT bar{BAR_LEFT_PIXELS + i * BAR_PITCH_PIXELS, BAR_TOP_PIXELS,
                                       BAR_LEFT_PIXELS + BAR_WIDTH_PIXELS + i * BAR_PITCH_PIXELS, BAR_BOTTOM_PIXELS};
                        if (!FillRect(dc, &bar, black))
                            throw std::runtime_error("Bar drawing failed");
                    }
                    else
                        Rectangle(dc, BAR_LEFT_PIXELS + i * BAR_PITCH_PIXELS, BAR_TOP_PIXELS,
                                  BAR_LEFT_PIXELS + BAR_WIDTH_PIXELS + i * BAR_PITCH_PIXELS, BAR_BOTTOM_PIXELS);
                }
                if (multi) {
                    if (bold) {
                        const RECT marker{MARKER_LEFT_PIXELS, MARKER_TOP_PIXELS,
                                          MARKER_BASE_RIGHT_PIXELS + page * MARKER_PAGE_STEP_PIXELS,
                                          MARKER_BOTTOM_PIXELS};
                        if (!FillRect(dc, &marker, black))
                            throw std::runtime_error("Page marker drawing failed");
                    }
                    else
                        Rectangle(dc, MARKER_LEFT_PIXELS, MARKER_TOP_PIXELS,
                                  MARKER_BASE_RIGHT_PIXELS + page * MARKER_PAGE_STEP_PIXELS, MARKER_BOTTOM_PIXELS);
                }
            }
            if (EndPage(dc) <= 0)
                throw std::runtime_error("EndPage failed");
        }
        if (EndDoc(dc) <= 0)
            throw std::runtime_error("EndDoc failed");
        started = false;
        DeleteDC(dc);
        std::cout << "GDI job completed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << "; Win32=" << GetLastError() << '\n';
        if (started)
            AbortDoc(dc);
        DeleteDC(dc);
        return 1;
    }
}
