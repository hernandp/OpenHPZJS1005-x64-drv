# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
#requires -Version 7.0
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$PackagePath,
    [Parameter(Mandatory)][ValidatePattern('^\d+\.\d+\.\d+\.\d+$')][string]$ExpectedVersion
)
$ErrorActionPreference = 'Stop'
foreach ($relative in @('driver/lj1005render.dll', 'driver/lj1005monitor.dll')) {
    $path = Join-Path $PackagePath $relative
    if (!(Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing binary: $relative" }
    # Reads PE resource data; does not load the module or run its entry point.
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo((Resolve-Path -LiteralPath $path).Path)
    $fileVersion = '{0}.{1}.{2}.{3}' -f $info.FileMajorPart,$info.FileMinorPart,$info.FileBuildPart,$info.FilePrivatePart
    $productVersion = '{0}.{1}.{2}.{3}' -f $info.ProductMajorPart,$info.ProductMinorPart,$info.ProductBuildPart,$info.ProductPrivatePart
    if ($fileVersion -ne $ExpectedVersion -or $productVersion -ne $ExpectedVersion -or
        $info.FileVersion -ne $ExpectedVersion -or $info.ProductVersion -ne $ExpectedVersion) {
        throw "Version resource missing or mismatched in $relative (expected $ExpectedVersion)."
    }
    if ($info.OriginalFilename -cne [IO.Path]::GetFileName($path) -or
        $info.ProductName -cne 'Project Benteveo' -or
        $info.CompanyName -cne 'Hernán Di Pietro' -or
        $info.LegalCopyright -cne 'Copyright (C) 2026 Hernán Di Pietro and Project Benteveo contributors' -or
        !$info.FileDescription -or !$info.InternalName) {
        throw "Version resource identity missing or mismatched in $relative."
    }
    if ($info.IsDebug -or !$info.IsPreRelease) {
        throw "Expected a Release configuration engineering preview: $relative"
    }
    [pscustomobject]@{
        path=$relative
        fileVersion=$fileVersion
        productVersion=$productVersion
        originalFilename=$info.OriginalFilename
        description=$info.FileDescription
        prerelease=$info.IsPreRelease
    }
}
