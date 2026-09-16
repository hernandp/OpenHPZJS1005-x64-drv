// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <winspool.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

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
        doc.lpszDocName = L"LaserJet 1005 GDI integration test";
        doc.lpszOutput = std::wstring(argv[2]) == L"-" ? nullptr : argv[2];
        if (StartDocW(dc, &doc) <= 0)
            throw std::runtime_error("StartDoc failed");
        started = true;
        const int pages = (multi || scenario == L"emf-reset") ? 3 : 1;
        for (int page = 0; page < pages; ++page) {
            if (scenario == L"emf-reset") {
                dm->dmPaperSize = page == 1 ? DMPAPER_A4 : DMPAPER_LETTER;
                dm->dmOrientation = page == 1 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
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
            else if (scenario != L"blank" && !(multi && page == 1)) {
                const int width = GetDeviceCaps(dc, HORZRES), height = GetDeviceCaps(dc, VERTRES);
                const auto black = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
                if (bold) {
                    // Half-millimetre strips, contained within the original border bounds.
                    const RECT edges[] = {{100, 100, width - 100, 112},
                                          {100, height - 112, width - 100, height - 100},
                                          {100, 112, 112, height - 112},
                                          {width - 112, 112, width - 100, height - 112}};
                    for (const auto& edge : edges)
                        if (!FillRect(dc, &edge, black))
                            throw std::runtime_error("Border drawing failed");
                }
                else
                    Rectangle(dc, 100, 100, width - 100, height - 100);
                HFONT font =
                    CreateFontW(-100, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH, L"Arial");
                auto previous = SelectObject(dc, font);
                std::wstring text = L"HP LaserJet 1005 - Windows x64 C++ driver";
                if (bold && multi)
                    text += L" - Page " + std::to_wstring(page + 1) + L" of 3";
                TextOutW(dc, 200, 300, text.c_str(), static_cast<int>(text.size()));
                SelectObject(dc, previous);
                DeleteObject(font);
                for (int i = 0; i < 16; ++i) {
                    if (bold) {
                        const RECT bar{200 + i * 200, 700, 280 + i * 200, 1100};
                        if (!FillRect(dc, &bar, black))
                            throw std::runtime_error("Bar drawing failed");
                    }
                    else
                        Rectangle(dc, 200 + i * 200, 700, 280 + i * 200, 1100);
                }
                if (multi) {
                    if (bold) {
                        const RECT marker{200, 1300, 300 + page * 200, 1500};
                        if (!FillRect(dc, &marker, black))
                            throw std::runtime_error("Page marker drawing failed");
                    }
                    else
                        Rectangle(dc, 200, 1300, 300 + page * 200, 1500);
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
