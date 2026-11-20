// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#include <windows.h>
#include <objbase.h>
#include <commctrl.h>
#include <compstui.h>
#pragma warning(push)
#pragma warning(disable : 4005)
#include <winddi.h>
#pragma warning(pop)
#include <winddiui.h>
#include <printoem.h>
#include <initguid.h>
#include <prcomoem.h>
#include "alignment_devmode.hpp"
#include "alignment_resource.h"
#include "about.hpp"
#include <atomic>
#include <memory>
#include <new>

namespace {
    namespace Alignment = Lj1005::Alignment;
    std::atomic<long> objects{0}, locks{0};
    constexpr int EDIT_LIMIT = 16;
    constexpr int LOCALE_SEPARATOR_CAPACITY = 8;

    struct Page {
        Alignment::PrivateDevMode* target;
        Alignment::Units units;
        HANDLE parent;
        PFNCOMPROPSHEET callback;
        LONG_PTR handle = 0;
        wchar_t separator[LOCALE_SEPARATOR_CAPACITY] = L".";
        bool updating = false;
    };

    void Display(HWND dialog, Page& page, Alignment::Shift shift) {
        const auto x = Alignment::Format(shift.x, page.units, page.separator);
        const auto y = Alignment::Format(shift.y, page.units, page.separator);
        page.updating = true;
        SetDlgItemTextW(dialog, IDC_HORIZONTAL_SHIFT, x.c_str());
        SetDlgItemTextW(dialog, IDC_VERTICAL_SHIFT, y.c_str());
        page.updating = false;
    }

    std::optional<Alignment::Shift> ReadShift(HWND dialog, const Page& page) {
        Alignment::Shift shift;
        for (const int field : {IDC_HORIZONTAL_SHIFT, IDC_VERTICAL_SHIFT}) {
            wchar_t value[EDIT_LIMIT + 1]{};
            GetDlgItemTextW(dialog, field, value, ARRAYSIZE(value));
            const auto parsed = Alignment::Parse(value, page.units, page.separator);
            if (!parsed) {
                const auto limit = Alignment::Format(Alignment::MAX_SHIFT_UM, page.units, page.separator);
                const auto message =
                    L"Enter a shift from -" + limit + L" to " + limit +
                    (page.units == Alignment::Units::inches ? L" in, using up to five decimal places."
                                                            : L" mm, using up to three decimal places.");
                MessageBoxW(dialog, message.c_str(), L"Benteveo Alignment", MB_OK | MB_ICONWARNING);
                SetFocus(GetDlgItem(dialog, field));
                return {};
            }
            (field == IDC_HORIZONTAL_SHIFT ? shift.x : shift.y) = *parsed;
        }
        return shift;
    }

    INT_PTR CALLBACK AlignmentDialog(HWND dialog, UINT message, WPARAM wParam, LPARAM lParam) try {
        if (message == WM_INITDIALOG) {
            auto& page = *reinterpret_cast<Page*>(reinterpret_cast<PROPSHEETPAGEW*>(lParam)->lParam);
            SetWindowLongPtrW(dialog, DWLP_USER, reinterpret_cast<LONG_PTR>(&page));
            if (!GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, page.separator, ARRAYSIZE(page.separator)))
                wcscpy_s(page.separator, L".");
            SendDlgItemMessageW(dialog, IDC_SHIFT_UNITS, CB_ADDSTRING, 0,
                                reinterpret_cast<LPARAM>(L"Millimeters (mm)"));
            SendDlgItemMessageW(dialog, IDC_SHIFT_UNITS, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Inches (in)"));
            SendDlgItemMessageW(dialog, IDC_SHIFT_UNITS, CB_SETCURSEL, static_cast<WPARAM>(page.units), 0);
            for (const int field : {IDC_HORIZONTAL_SHIFT, IDC_VERTICAL_SHIFT})
                SendDlgItemMessageW(dialog, field, EM_SETLIMITTEXT, EDIT_LIMIT, 0);
            Display(dialog, page, Alignment::ReadDevMode(page.target).shift);
            return TRUE;
        }
        auto* page = reinterpret_cast<Page*>(GetWindowLongPtrW(dialog, DWLP_USER));
        if (!page || page->updating)
            return FALSE;
        if (message == WM_COMMAND && LOWORD(wParam) == IDC_SHIFT_UNITS && HIWORD(wParam) == CBN_SELCHANGE) {
            if (const auto shift = ReadShift(dialog, *page)) {
                const auto selected = SendDlgItemMessageW(dialog, IDC_SHIFT_UNITS, CB_GETCURSEL, 0, 0);
                page->units = selected == static_cast<LRESULT>(Alignment::Units::inches)
                                  ? Alignment::Units::inches
                                  : Alignment::Units::millimeters;
                Display(dialog, *page, *shift);
                PropSheet_Changed(GetParent(dialog), dialog);
            }
            else {
                SendDlgItemMessageW(dialog, IDC_SHIFT_UNITS, CB_SETCURSEL, static_cast<WPARAM>(page->units), 0);
            }
            return TRUE;
        }
        if (message == WM_COMMAND && HIWORD(wParam) == EN_CHANGE &&
            (LOWORD(wParam) == IDC_HORIZONTAL_SHIFT || LOWORD(wParam) == IDC_VERTICAL_SHIFT)) {
            PropSheet_Changed(GetParent(dialog), dialog);
            return TRUE;
        }
        if (message == WM_NOTIFY && reinterpret_cast<const NMHDR*>(lParam)->code == PSN_APPLY) {
            const auto shift = ReadShift(dialog, *page);
            if (!shift) {
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_INVALID);
                return TRUE;
            }
            const auto previous = *page->target;
            auto updated = Alignment::DefaultDevMode();
            updated.shift = *shift;
            updated.units = page->units;
            *page->target = updated;
            if (page->callback(page->parent, CPSFUNC_SET_RESULT, page->handle, CPSUI_OK) < 0) {
                *page->target = previous;
                SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_INVALID);
                return TRUE;
            }
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_NOERROR);
            return TRUE;
        }
        return FALSE;
    }

    catch (...) {
        if (message == WM_NOTIFY && reinterpret_cast<const NMHDR*>(lParam)->code == PSN_APPLY) {
            SetWindowLongPtrW(dialog, DWLP_MSGRESULT, PSNRET_INVALID);
            return TRUE;
        }
        return FALSE;
    }

    class Ui final : public IPrintOemUI {
        std::atomic<ULONG> refs{1};

    public:
        Ui() {
            ++objects;
        }

        ~Ui() {
            --objects;
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (id != IID_IUnknown && id != IID_IPrintOemUI && id != IID_IPrintOemCommon)
                return E_NOINTERFACE;
            *out = static_cast<IPrintOemUI*>(this);
            AddRef();
            return S_OK;
        }

        ULONG STDMETHODCALLTYPE AddRef() override {
            return ++refs;
        }

        ULONG STDMETHODCALLTYPE Release() override {
            const auto remaining = --refs;
            if (!remaining)
                delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE GetInfo(DWORD mode, PVOID buffer, DWORD size, PDWORD needed) override {
            return Alignment::GetInfo(mode, buffer, size, needed);
        }

        HRESULT STDMETHODCALLTYPE DevMode(DWORD mode, POEMDMPARAM param) override {
            return Alignment::DevMode(mode, param);
        }

        HRESULT STDMETHODCALLTYPE PublishDriverInterface(IUnknown*) override {
            return S_OK;
        }

        HRESULT STDMETHODCALLTYPE CommonUIProp(DWORD, POEMCUIPPARAM) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DocumentPropertySheets(PPROPSHEETUI_INFO info, LPARAM param) override {
            if (!info)
                return E_POINTER;
            switch (info->Reason) {
            case PROPSHEETUI_REASON_INIT: {
                const auto* input = reinterpret_cast<const OEMUIPSPARAM*>(info->lParamInit);
                if (!input || !Alignment::HasCurrentDevMode(input->pOEMDM) || !info->pfnComPropSheet)
                    return E_INVALIDARG;
                auto* target = static_cast<Alignment::PrivateDevMode*>(input->pOEMDM);
                std::unique_ptr<Page> page(new (std::nothrow) Page{target, Alignment::ReadDevMode(target).units,
                                                                   info->hComPropSheet, info->pfnComPropSheet});
                if (!page)
                    return E_OUTOFMEMORY;
                PROPSHEETPAGEW definition{};
                definition.dwSize = sizeof(definition);
                definition.hInstance = static_cast<HINSTANCE>(input->hModule);
                definition.pszTemplate = MAKEINTRESOURCEW(IDD_ALIGNMENT);
                definition.pfnDlgProc = AlignmentDialog;
                definition.lParam = reinterpret_cast<LPARAM>(page.get());
                const auto handle = CreatePropertySheetPageW(&definition);
                if (!handle)
                    return E_FAIL;
                page->handle =
                    page->callback(page->parent, CPSFUNC_ADD_HPROPSHEETPAGE, reinterpret_cast<LPARAM>(handle), 0);
                if (!page->handle) {
                    DestroyPropertySheetPage(handle);
                    return E_FAIL;
                }
                if (const auto about = Lj1005::CreateAboutPage(definition.hInstance)) {
                    if (!page->callback(page->parent, CPSFUNC_ADD_HPROPSHEETPAGE, reinterpret_cast<LPARAM>(about), 0))
                        DestroyPropertySheetPage(about);
                }
                info->UserData = reinterpret_cast<ULONG_PTR>(page.release());
                info->Result = 1;
                return S_OK;
            }
            case PROPSHEETUI_REASON_SET_RESULT:
                info->Result = reinterpret_cast<const SETRESULT_INFO*>(param)->Result;
                return S_OK;
            case PROPSHEETUI_REASON_DESTROY:
                delete reinterpret_cast<Page*>(info->UserData);
                info->UserData = 0;
                return S_OK;
            default:
                return E_NOTIMPL;
            }
        }

        HRESULT STDMETHODCALLTYPE DevicePropertySheets(PPROPSHEETUI_INFO, LPARAM) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DevQueryPrintEx(POEMUIOBJ, PDEVQUERYPRINT_INFO, PDEVMODE, PVOID) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DeviceCapabilities(POEMUIOBJ, HANDLE, PWSTR, WORD, PVOID, PDEVMODE, PVOID, DWORD,
                                                     DWORD*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE UpgradePrinter(DWORD, PBYTE) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE PrinterEvent(PWSTR, INT, DWORD, LPARAM) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE DriverEvent(DWORD, DWORD, LPBYTE, LPARAM) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE QueryColorProfile(HANDLE, POEMUIOBJ, PDEVMODE, PVOID, ULONG, VOID*, ULONG*,
                                                    FLONG*) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE FontInstallerDlgProc(HWND, UINT, WPARAM, LPARAM) override {
            return E_NOTIMPL;
        }

        HRESULT STDMETHODCALLTYPE UpdateExternalFonts(HANDLE, HANDLE, PWSTR) override {
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
            const auto remaining = --refs;
            if (!remaining)
                delete this;
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** out) override {
            if (!out)
                return E_POINTER;
            *out = nullptr;
            if (outer)
                return CLASS_E_NOAGGREGATION;
            auto* ui = new (std::nothrow) Ui;
            if (!ui)
                return E_OUTOFMEMORY;
            const auto hr = ui->QueryInterface(id, out);
            ui->Release();
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
    if (clsid != CLSID_OEMUI)
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
