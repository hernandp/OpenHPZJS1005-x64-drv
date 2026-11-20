// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include <windows.h>
#include <objbase.h>
#pragma warning(push)
#pragma warning(disable : 4005)
#include <winddi.h>
#pragma warning(pop)
#include <compstui.h>
#define KERNEL_MODE
#include <printoem.h>
#include <initguid.h>
#include <prcomoem.h>
#include <iostream>
#include <stdexcept>
#include "lj1005/protocol.hpp"
#include "../driver/alignment_devmode.hpp"
#include "../driver/alignment_resource.h"

class Capture final : public IPrintOemDriverUni {
public:
    Lj1005::Bytes bytes;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (!out)
            return E_POINTER;
        *out = nullptr;
        if (id != IID_IUnknown && id != IID_IPrintOemDriverUni)
            return E_NOINTERFACE;
        *out = this;
        return S_OK;
    }

    ULONG STDMETHODCALLTYPE AddRef() override {
        return 2;
    }

    ULONG STDMETHODCALLTYPE Release() override {
        return 1;
    }

    HRESULT STDMETHODCALLTYPE DrvWriteSpoolBuf(PDEVOBJ, PVOID data, DWORD count, DWORD* written) override {
        auto* first = static_cast<std::uint8_t*>(data);
        bytes.insert(bytes.end(), first, first + count);
        *written = count;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE DrvGetDriverSetting(PVOID, PCSTR, PVOID, DWORD, PDWORD, PDWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvXMoveTo(PDEVOBJ, INT, DWORD, INT*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvYMoveTo(PDEVOBJ, INT, DWORD, INT*) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvGetStandardVariable(PDEVOBJ, DWORD, PVOID, DWORD, PDWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvGetGPDData(PDEVOBJ, DWORD, PVOID, PVOID, DWORD, PDWORD) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvUniTextOut(SURFOBJ*, STROBJ*, FONTOBJ*, CLIPOBJ*, RECTL*, RECTL*, BRUSHOBJ*, BRUSHOBJ*,
                                            POINTL*, MIX) override {
        return E_NOTIMPL;
    }

    HRESULT STDMETHODCALLTYPE DrvWriteAbortBuf(PDEVOBJ, PVOID, DWORD, DWORD) override {
        return E_NOTIMPL;
    }
};

void Check(HRESULT hr, const char* name) {
    if (FAILED(hr)) {
        std::cerr << name << " HRESULT=" << std::hex << hr << '\n';
        throw std::runtime_error(name);
    }
}

void CheckAlignment(IPrintOemUni& render, const wchar_t* uiPath) {
    namespace Alignment = Lj1005::Alignment;
    const auto library = LoadLibraryW(uiPath);
    if (!library || !FindResourceW(library, MAKEINTRESOURCEW(IDD_ALIGNMENT), RT_DIALOG))
        throw std::runtime_error("Alignment UI or dialog resource missing");
    using GetClass = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
    const auto get = reinterpret_cast<GetClass>(GetProcAddress(library, "DllGetClassObject"));
    if (!get)
        throw std::runtime_error("Alignment UI COM export missing");
    IClassFactory* factory = nullptr;
    Check(get(CLSID_OEMUI, IID_IClassFactory, reinterpret_cast<void**>(&factory)), "UI class factory");
    IUnknown* uiInterface = nullptr;
    Check(factory->CreateInstance(nullptr, IID_IPrintOemUI, reinterpret_cast<void**>(&uiInterface)),
          "Create alignment UI");
    factory->Release();
    IPrintOemCommon* ui = nullptr;
    Check(uiInterface->QueryInterface(IID_IPrintOemCommon, reinterpret_cast<void**>(&ui)), "Query alignment DevMode");
    uiInterface->Release();

    Alignment::PrivateDevMode settings{};
    OEMDMPARAM dm{};
    dm.cbSize = sizeof(dm);
    dm.pOEMDMOut = &settings;
    Check(ui->DevMode(OEMDM_SIZE, &dm), "UI DevMode size");
    if (dm.cbBufSize != sizeof(settings))
        throw std::runtime_error("Unexpected UI DevMode size");
    Check(ui->DevMode(OEMDM_DEFAULT, &dm), "UI DevMode defaults");
    if (settings.shift.x || settings.shift.y || settings.units != Alignment::Units::millimeters)
        throw std::runtime_error("Alignment defaults must be zero millimeters");
    settings.shift = {-200000, 2540};
    settings.units = Alignment::Units::inches;
    dm.pOEMDMIn = &settings;
    Check(render.DevMode(OEMDM_MERGE, &dm), "Renderer merges UI preferences");
    if (settings.shift.x != -200000 || settings.shift.y != 2540 || settings.units != Alignment::Units::inches)
        throw std::runtime_error("Alignment preferences lost during merge");
    OEM_DMEXTRAHEADER old{sizeof(old), settings.header.dwSignature, 1};
    dm.pOEMDMIn = &old;
    Check(render.DevMode(OEMDM_CONVERT, &dm), "Upgrade old DevMode");
    if (settings.shift.x || settings.shift.y || settings.units != Alignment::Units::millimeters)
        throw std::runtime_error("Old DevMode did not migrate to zero millimeters");
    ui->Release();
    FreeLibrary(library);

    for (const auto value : {-200000, -12345, -1, 0, 1, 2540, 12345, 200000}) {
        for (const auto unit : {Alignment::Units::millimeters, Alignment::Units::inches})
            if (Alignment::Parse(Alignment::Format(value, unit, L","), unit, L",") != value)
                throw std::runtime_error("Offset unit conversion lost precision");
    }
    if (Alignment::Parse(L"1,25", Alignment::Units::millimeters, L",") != 1250 ||
        Alignment::Parse(L"200.001", Alignment::Units::millimeters, L".") ||
        Alignment::Parse(L"-7.8741", Alignment::Units::inches, L".") ||
        Alignment::Parse(L"1.2.3", Alignment::Units::millimeters, L".") ||
        Alignment::DeviceDots({20, 22}, false).x != 0 || Alignment::DeviceDots({20, 22}, false).y != 1)
        throw std::runtime_error("Offset parsing, bounds or dot rounding failed");
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3)
        return 1;
    auto library = LoadLibraryW(argv[1]);
    if (!library) {
        std::cerr << "LoadLibrary=" << GetLastError() << '\n';
        return 1;
    }
    try {
        using GetClass = HRESULT(__stdcall*)(REFCLSID, REFIID, void**);
        auto get = reinterpret_cast<GetClass>(GetProcAddress(library, "DllGetClassObject"));
        if (!get)
            throw std::runtime_error("Missing DllGetClassObject export");
        IClassFactory* factory = nullptr;
        Check(get(CLSID_OEMRENDER, IID_IClassFactory, reinterpret_cast<void**>(&factory)), "Class factory");
        IPrintOemUni* render = nullptr;
        Check(factory->CreateInstance(nullptr, IID_IPrintOemUni, reinterpret_cast<void**>(&render)), "CreateInstance");
        factory->Release();
        for (DWORD mode = 1; mode <= 3; ++mode) {
            DWORD value = 0, size = 0;
            Check(render->GetInfo(mode, &value, sizeof(value), &size), "GetInfo");
            std::cout << "GetInfo(" << mode << ")=" << std::hex << value << '\n';
        }
        CheckAlignment(*render, argv[2]);
        DRVENABLEDATA enabled{};
        Check(render->EnableDriver(DDI_DRIVER_VERSION_NT5, sizeof(enabled), &enabled), "EnableDriver");
        Capture capture;
        Check(render->PublishDriverInterface(&capture), "Publish helper");
        DEVMODEW publicDm[3]{};
        DEVOBJ device[3]{};
        auto zero = Lj1005::Alignment::DefaultDevMode();
        auto landscape = zero;
        landscape.shift = {2540, -1270};
        auto clipped = zero;
        clipped.shift = {-5080, 2540};
        device[0].pOEMDM = &zero;
        device[1].pOEMDM = &landscape;
        device[2].pOEMDM = &clipped;
        for (int i = 0; i < 3; ++i) {
            publicDm[i].dmPaperSize = i == 1 ? DMPAPER_A4 : DMPAPER_LETTER;
            publicDm[i].dmOrientation = i == 1 ? DMORIENT_LANDSCAPE : DMORIENT_PORTRAIT;
            device[i].pPublicDM = &publicDm[i];
            GDIINFO info{};
            info.ulLogPixelsX = info.ulLogPixelsY = 600;
            info.ulHorzRes = i == 1 ? 6826 : 4910;
            info.ulVertRes = i == 1 ? 4770 : 6410;
            info.ptlPhysOffset = {95, 95};
            Check(render->EnablePDEV(&device[i], nullptr, 0, nullptr, sizeof(info), &info, 0, nullptr, nullptr,
                                     &device[i].pdevOEM),
                  "Enable PDEV");
            if (i)
                Check(render->ResetPDEV(&device[i - 1], &device[i]), "Reset PDEV");
            INT result = 0;
            // DOC_SETUP may be reissued by Unidrv following ResetDC, without EndDoc.
            Check(render->CommandCallback(&device[i], 1, 0, nullptr, &result), "Start/reset document");
            Check(render->CommandCallback(&device[i], 2, 0, nullptr, &result), "Start page");
            Lj1005::Bytes row(i == 1 ? (4770 + 7) / 8 : (4910 + 7) / 8, 0xff);
            Check(render->FilterGraphics(&device[i], row.data(), static_cast<DWORD>(row.size())), "Raster row");
            Check(render->CommandCallback(&device[i], 3, 0, nullptr, &result), "End page");
        }
        INT result = 0;
        Check(render->CommandCallback(&device[2], 4, 0, nullptr, &result), "End document");
        const auto pages = Lj1005::DecodeJob(capture.bytes);
        if (pages.size() != 3 || pages[0].width != 5100 || pages[1].width != 4960 || pages[1].height != 7016 ||
            pages[2].width != 5100)
            throw std::runtime_error("Paper reset damaged document geometry");
        auto expected = Lj1005::BeginDocument();
        const int startX[] = {95, 125, 0};
        const int endX[] = {5005, 4895, 4885};
        const int y[] = {95, 155, 155};
        for (std::size_t i = 0; i < pages.size(); ++i) {
            Lj1005::PageOptions options;
            options.paper = i == 1 ? 9 : 1;
            auto expectedPage = pages[i];
            expectedPage.pixels.assign(expectedPage.pixels.size(), 0);
            const auto stride = (expectedPage.width + 7) / 8;
            for (auto x = startX[i]; x < endX[i]; ++x)
                expectedPage.pixels[std::size_t(y[i]) * stride + x / 8] |= static_cast<std::uint8_t>(0x80 >> (x % 8));
            const auto encoded = Lj1005::EncodePage(expectedPage, options, static_cast<std::uint32_t>(i + 1));
            expected.insert(expected.end(), encoded.begin(), encoded.end());
        }
        const auto end = Lj1005::EndDocument();
        expected.insert(expected.end(), end.begin(), end.end());
        if (expected != capture.bytes)
            throw std::runtime_error("Zero shift, landscape shift, or clipping differs from expected output");
        // A second independent job on the same PDEV must still get its own header.
        capture.bytes.clear();
        for (DWORD command : {1, 2, 3, 4})
            Check(render->CommandCallback(&device[2], command, 0, nullptr, &result), "Second job");
        if (Lj1005::DecodeJob(capture.bytes).size() != 1)
            throw std::runtime_error("Second job lost its header");
        for (auto& dev : device)
            Check(render->DisablePDEV(&dev), "Disable PDEV");
        render->DisableDriver();
        render->Release();
        FreeLibrary(library);
        std::cout << "Render COM initialization passed\n";
        return 0;
    }
    catch (const std::exception& e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
