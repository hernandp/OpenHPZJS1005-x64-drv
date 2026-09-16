// SPDX-License-Identifier: GPL-2.0-or-later
#include <windows.h>
#include <objbase.h>
#pragma warning(push)
#pragma warning(disable : 4005) // SDK ddraw.h duplicates CO_E_NOTINITIALIZED.
#include <winddi.h>
#pragma warning(pop)
#include <compstui.h>
// Historical SDK switch exposes rendering types; this DLL runs entirely in user mode.
#define KERNEL_MODE
#include <printoem.h>
#include <initguid.h>
#include <prcomoem.h>
#include "lj1005/protocol.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <memory>
#include <new>
#include <stdexcept>

namespace {
    std::atomic<long> objects{0}, locks{0};
    constexpr DWORD SIGNATURE = 0x35303031; // "1005"

    struct Device {
        lj1005::Raster raster;
        lj1005::PageOptions options;
        std::uint32_t originX = 0, originY = 0;
        std::uint32_t row = 0, page = 0;
        bool inDocument = false, inPage = false, failed = false;
    };

    class Render final : public IPrintOemUni {
        std::atomic<ULONG> refs{1};
        IPrintOemDriverUni* helper = nullptr;

        HRESULT Spool(PDEVOBJ dev, lj1005::Bytes bytes) {
            if (!helper)
                return E_UNEXPECTED;
            std::size_t pos = 0;
            while (pos < bytes.size()) {
                DWORD written = 0;
                const auto count = static_cast<DWORD>(std::min<std::size_t>(65536, bytes.size() - pos));
                auto hr = helper->DrvWriteSpoolBuf(dev, bytes.data() + pos, count, &written);
                if (FAILED(hr))
                    return hr;
                if (!written || written > count)
                    return E_FAIL;
                pos += written;
            }
            return S_OK;
        }

    public:
        Render() {
            ++objects;
        }

        ~Render() {
            if (helper)
                helper->Release();
            --objects;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (id == IID_IUnknown || id == IID_IPrintOemUni || id == IID_IPrintOemEngine ||
                id == IID_IPrintOemCommon) {
                *out = static_cast<IPrintOemUni*>(this);
                AddRef();
                return S_OK;
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++refs;
        }

        ULONG STDMETHODCALLTYPE Release() override {
            auto n = --refs;
            if (!n)
                delete this;
            return n;
        }

        HRESULT STDMETHODCALLTYPE GetInfo(DWORD mode, PVOID buffer, DWORD size, PDWORD needed) override {
            if (!needed)
                return E_POINTER;
            *needed = sizeof(DWORD);
            DWORD value;
            switch (mode) {
            case OEMGI_GETSIGNATURE:
                value = SIGNATURE;
                break;
            case OEMGI_GETINTERFACEVERSION:
                value = PRINTER_OEMINTF_VERSION;
                break;
            case OEMGI_GETVERSION:
                value = 0x00010000;
                break;
            default:
                *needed = 0;
                return E_NOTIMPL;
            }
            if (!buffer || size < sizeof(value)) {
                SetLastError(ERROR_INSUFFICIENT_BUFFER);
                return E_FAIL;
            }
            std::memcpy(buffer, &value, sizeof(value));
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DevMode(DWORD mode, POEMDMPARAM param) override {
            if (!param)
                return E_POINTER;
            if (mode == OEMDM_SIZE) {
                param->cbBufSize = sizeof(OEM_DMEXTRAHEADER);
                return S_OK;
            }
            if (mode != OEMDM_DEFAULT && mode != OEMDM_CONVERT && mode != OEMDM_MERGE)
                return E_NOTIMPL;
            if (!param->pOEMDMOut || param->cbBufSize < sizeof(OEM_DMEXTRAHEADER))
                return E_INVALIDARG;
            *static_cast<OEM_DMEXTRAHEADER*>(param->pOEMDMOut) = {sizeof(OEM_DMEXTRAHEADER), SIGNATURE, 1};
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE EnableDriver(DWORD, DWORD size, PDRVENABLEDATA data) override {
            if (!data || size < sizeof(*data))
                return E_INVALIDARG;
            data->iDriverVersion = DDI_DRIVER_VERSION_NT5;
            data->c = 0;
            data->pdrvfn = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DisableDriver() override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE EnablePDEV(PDEVOBJ dev, PWSTR, ULONG, HSURF*, ULONG infoSize, GDIINFO* info, ULONG,
                                             DEVINFO*, DRVENABLEDATA*, PDEVOEM* out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (!dev || !info || infoSize < sizeof(GDIINFO) || !dev->pPublicDM)
                return E_INVALIDARG;
            if (info->ulLogPixelsX != 600 || info->ulLogPixelsY != 600 || !info->ulHorzRes || !info->ulVertRes ||
                info->ulHorzRes > 10000 || info->ulVertRes > 10000)
                return E_INVALIDARG;
            try {
                auto d = std::make_unique<Device>();
                d->raster.width = info->ulHorzRes;
                d->raster.height = info->ulVertRes;
                // LANDSCAPE_CC270 rotates into portrait device coordinates before FilterGraphics.
                if (dev->pPublicDM->dmOrientation == DMORIENT_LANDSCAPE)
                    std::swap(d->raster.width, d->raster.height);
                d->options.paper = dev->pPublicDM->dmPaperSize == DMPAPER_LETTER ? 1 : 9;
                d->originX = static_cast<std::uint32_t>(std::max<LONG>(0, info->ptlPhysOffset.x));
                d->originY = static_cast<std::uint32_t>(std::max<LONG>(0, info->ptlPhysOffset.y));
                // MaxCopies=1: Unidrv performs copy expansion, keeping document ordering consistent.
                *out = static_cast<PDEVOEM>(d.release());
                return S_OK;
            }
            catch (...) {
                return E_OUTOFMEMORY;
            }
        }

        HRESULT STDMETHODCALLTYPE DisablePDEV(PDEVOBJ dev) override {
            if (!dev)
                return E_POINTER;
            delete static_cast<Device*>(dev->pdevOEM);
            dev->pdevOEM = nullptr;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE ResetPDEV(PDEVOBJ oldDev, PDEVOBJ newDev) override {
            if (!oldDev || !newDev || !oldDev->pdevOEM || !newDev->pdevOEM)
                return E_INVALIDARG;
            auto& old = *static_cast<Device*>(oldDev->pdevOEM);
            auto& next = *static_cast<Device*>(newDev->pdevOEM);
            if (old.inPage)
                return E_FAIL;
            next.page = old.page;
            next.inDocument = old.inDocument;
            next.failed = old.failed;
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE PublishDriverInterface(IUnknown* unknown) override {
            if (!unknown)
                return E_POINTER;
            if (helper)
                return S_OK;
            return unknown->QueryInterface(IID_IPrintOemDriverUni, reinterpret_cast<void**>(&helper));
        }

        HRESULT STDMETHODCALLTYPE GetImplementedMethod(PSTR name) override {
            if (!name)
                return E_POINTER;
            return !std::strcmp(name, "CommandCallback") || !std::strcmp(name, "FilterGraphics") ? S_OK : S_FALSE;
        }

        HRESULT STDMETHODCALLTYPE CommandCallback(PDEVOBJ dev, DWORD id, DWORD count, PDWORD params,
                                                  INT* result) override {
            if (!dev || !dev->pdevOEM || !result)
                return E_POINTER;
            *result = 0;
            auto& d = *static_cast<Device*>(dev->pdevOEM);
            try {
                HRESULT hr = S_OK;
                if (id == 1) {
                    // Unidrv repeats DOC_SETUP after ResetDC. ResetPDEV carries the
                    // active document across: never inject another JZJZ/START_DOC.
                    if (d.inDocument)
                        return (!d.inPage && !d.failed) ? S_OK : E_FAIL;
                    d.page = 0;
                    d.failed = false;
                    d.inPage = false;
                    d.inDocument = true;
                    hr = Spool(dev, lj1005::BeginDocument());
                }
                else if (id == 2) {
                    if (!d.inDocument || d.inPage || d.failed)
                        return E_FAIL;
                    d.raster.pixels.assign(std::size_t((d.raster.width + 7) / 8) * d.raster.height, 0);
                    d.row = 0;
                    d.inPage = true;
                }
                else if (id == 3) {
                    if (!d.inPage || d.failed) {
                        d.failed = true;
                        return E_FAIL;
                    }
                    // Send the full physical sheet, as on the hardware-verified diagnostic path.
                    // Preserve Unidrv margins in pixels instead of emitting device offset commands.
                    auto sheet = lj1005::PlaceOnSheet(d.raster, d.options.paper == 1 ? 5100 : 4960,
                                                      d.options.paper == 1 ? 6600 : 7016, d.originX, d.originY);
                    hr = Spool(dev, lj1005::EncodePage(sheet, d.options, ++d.page));
                    d.inPage = false;
                }
                else if (id == 4) {
                    if (!d.inDocument || d.inPage || d.failed)
                        return E_FAIL;
                    hr = Spool(dev, lj1005::EndDocument());
                    d.inDocument = false;
                }
                else if (id == 5) {
                    // Full-width scan lines start at the printable origin. No horizontal blank stripping.
                    if (count != 1 || !params)
                        return E_INVALIDARG;
                }
                else if (id == 6) {
                    if (count != 1 || !params)
                        return E_INVALIDARG;
                    // Unidrv can omit wholly white leading/trailing bands. Preserve their page positions.
                    // DestY is relative to Unidrv's printable cursor origin, not physical paper zero.
                    if (params[0] > d.raster.height) {
                        d.failed = true;
                        return E_FAIL;
                    }
                    d.row = params[0];
                }
                else
                    return E_NOTIMPL;
                if (FAILED(hr))
                    d.failed = true;
                return hr;
            }
            catch (...) {
                d.failed = true;
                return E_FAIL;
            }
        }

        HRESULT STDMETHODCALLTYPE FilterGraphics(PDEVOBJ dev, PBYTE data, DWORD length) override {
            if (!dev || !dev->pdevOEM || !data)
                return E_POINTER;
            auto& d = *static_cast<Device*>(dev->pdevOEM);
            const auto stride = (d.raster.width + 7) / 8;
            if (!d.inPage || d.failed || d.row >= d.raster.height ||
                (length != stride && length != (stride + 3) / 4 * 4)) {
                d.failed = true;
                return E_FAIL;
            }
            std::memcpy(d.raster.pixels.data() + std::size_t(d.row++) * stride, data, stride);
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE DriverDMS(PVOID, PVOID, DWORD, PDWORD) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE ImageProcessing(PDEVOBJ, PBYTE, PBITMAPINFOHEADER, PBYTE, DWORD, PIPPARAMS,
                                                  PBYTE*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE Compression(PDEVOBJ, PBYTE, PBYTE, DWORD, DWORD, INT*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE HalftonePattern(PDEVOBJ, PBYTE, DWORD, DWORD, DWORD, DWORD, PBYTE, DWORD) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE MemoryUsage(PDEVOBJ, POEMMEMORYUSAGE) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE TTYGetInfo(PDEVOBJ, DWORD, PVOID, DWORD, DWORD*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DownloadFontHeader(PDEVOBJ, PUNIFONTOBJ, DWORD*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DownloadCharGlyph(PDEVOBJ, PUNIFONTOBJ, HGLYPH, PDWORD, DWORD*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE TTDownloadMethod(PDEVOBJ, PUNIFONTOBJ, DWORD*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE OutputCharStr(PDEVOBJ, PUNIFONTOBJ, DWORD, DWORD, PVOID) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE SendFontCmd(PDEVOBJ, PUNIFONTOBJ, PFINVOCATION) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE TextOutAsBitmap(SURFOBJ*, STROBJ*, FONTOBJ*, CLIPOBJ*, RECTL*, RECTL*, BRUSHOBJ*,
                                                  BRUSHOBJ*, POINTL*, MIX) override {
            return E_NOTIMPL;
        }
    };

    class Factory final : public IClassFactory {
        std::atomic<ULONG> refs{1};

    public:
        Factory() {
            ++objects;
        }

        ~Factory() {
            --objects;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (id != IID_IUnknown && id != IID_IClassFactory)
                return E_NOINTERFACE;
            *out = static_cast<IClassFactory*>(this);
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++refs;
        }

        ULONG STDMETHODCALLTYPE Release() override {
            auto n = --refs;
            if (!n)
                delete this;
            return n;
        }

        HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (outer)
                return CLASS_E_NOAGGREGATION;
            auto* instance = new (std::nothrow) Render;
            if (!instance)
                return E_OUTOFMEMORY;
            const auto hr = instance->QueryInterface(id, out);
            instance->Release();
            return hr;
        }

        HRESULT STDMETHODCALLTYPE LockServer(BOOL lock) override {
            if (lock)
                ++locks;
            else
                --locks;
            return S_OK;
        }
    };
}

extern "C" HRESULT __stdcall DllGetClassObject(REFCLSID clsid, REFIID id, void** out) {
    if (!out)
        return E_POINTER;
    *out = nullptr;
    if (clsid != CLSID_OEMRENDER)
        return CLASS_E_CLASSNOTAVAILABLE;
    auto* factory = new (std::nothrow) Factory;
    if (!factory)
        return E_OUTOFMEMORY;
    const auto hr = factory->QueryInterface(id, out);
    factory->Release();
    return hr;
}

extern "C" HRESULT __stdcall DllCanUnloadNow() {
    return objects == 0 && locks == 0 ? S_OK : S_FALSE;
}
