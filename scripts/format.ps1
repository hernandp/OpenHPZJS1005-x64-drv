param([switch]$Check, [string]$ClangFormat)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
if (!$ClangFormat) {
    $command = Get-Command clang-format -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($command) { $ClangFormat = $command.Source }
}
if (!$ClangFormat) {
    $vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere) {
        $vs = & $vswhere -latest -products * -property installationPath
        if ($vs) {
            $candidate = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-format.exe'
            if (Test-Path -LiteralPath $candidate) { $ClangFormat = $candidate }
        }
    }
}
if (!$ClangFormat) { throw 'Install clang-format 22, or pass its executable path with -ClangFormat.' }
$version = & $ClangFormat --version
if ($LASTEXITCODE -ne 0 -or $version -notmatch '\bversion 22\.') {
    throw "Use clang-format 22 for reproducible formatting. Found: $version"
}
# Only project-owned C++ sources and headers: never format the vendored C codec,
# reference C adapters, generated files, downloaded sources, or local captures.
$files = @('src','include','driver','tests') | ForEach-Object {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot $_) -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp','.hpp') } |
        Select-Object -ExpandProperty FullName
}
$arguments = @('--style=file', '--fallback-style=none')
if ($Check) { $arguments += @('--dry-run','--Werror') } else { $arguments += '-i' }
& $ClangFormat @arguments @files
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting check or update failed.' }
Write-Output "$(if ($Check) { 'Checked' } else { 'Formatted' }) $($files.Count) C++ files with $version"
