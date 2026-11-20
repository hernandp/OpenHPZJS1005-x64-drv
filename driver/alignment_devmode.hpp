// SPDX-License-Identifier: GPL-2.0-or-later
// See LICENSE in the repository root for license terms.
#pragma once

#include <cstring>
#include "alignment.hpp"

namespace Lj1005::Alignment {
    inline constexpr DWORD SIGNATURE = 0x35303031; // "1005"
    inline constexpr DWORD DEVMODE_VERSION = 2;
    inline constexpr DWORD PLUGIN_VERSION = 0x00010000;

    struct PrivateDevMode {
        OEM_DMEXTRAHEADER header;
        Shift shift;
        Units units;
    };

    inline PrivateDevMode DefaultDevMode() {
        return {{sizeof(PrivateDevMode), SIGNATURE, DEVMODE_VERSION}, {}, Units::millimeters};
    }

    inline bool HasCurrentDevMode(const void* input) {
        if (!input)
            return false;
        const auto& header = *static_cast<const OEM_DMEXTRAHEADER*>(input);
        return header.dwSignature == SIGNATURE && header.dwVersion == DEVMODE_VERSION &&
               header.dwSize == sizeof(PrivateDevMode);
    }

    inline PrivateDevMode ReadDevMode(const void* input) {
        auto result = DefaultDevMode();
        // Version 1 only stored the header. Missing and unknown versions use defaults too.
        if (HasCurrentDevMode(input)) {
            const auto& saved = *static_cast<const PrivateDevMode*>(input);
            if (IsValid(saved.shift.x) && IsValid(saved.shift.y))
                result.shift = saved.shift;
            if (saved.units == Units::inches)
                result.units = saved.units;
        }
        return result;
    }

    inline HRESULT DevMode(DWORD mode, POEMDMPARAM param) {
        if (!param)
            return E_POINTER;
        if (mode == OEMDM_SIZE) {
            param->cbBufSize = sizeof(PrivateDevMode);
            return S_OK;
        }
        if (mode != OEMDM_DEFAULT && mode != OEMDM_CONVERT && mode != OEMDM_MERGE)
            return E_NOTIMPL;
        if (!param->pOEMDMOut || param->cbBufSize < sizeof(PrivateDevMode))
            return E_INVALIDARG;
        const auto settings = mode == OEMDM_DEFAULT ? DefaultDevMode() : ReadDevMode(param->pOEMDMIn);
        std::memcpy(param->pOEMDMOut, &settings, sizeof(settings));
        return S_OK;
    }

    inline HRESULT GetInfo(DWORD mode, PVOID buffer, DWORD size, PDWORD needed) {
        if (!needed)
            return E_POINTER;
        *needed = sizeof(DWORD);
        DWORD value = 0;
        switch (mode) {
        case OEMGI_GETSIGNATURE:
            value = SIGNATURE;
            break;
        case OEMGI_GETINTERFACEVERSION:
            value = PRINTER_OEMINTF_VERSION;
            break;
        case OEMGI_GETVERSION:
            value = PLUGIN_VERSION;
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
}
