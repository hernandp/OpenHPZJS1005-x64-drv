# SPDX-License-Identifier: GPL-2.0-or-later
# See LICENSE in the repository root for license terms.
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
$files = @(foreach ($directory in @('src','include','driver','tests')) {
    Get-ChildItem -LiteralPath (Join-Path $projectRoot $directory) -Recurse -File |
        Where-Object { $_.Extension -in @('.cpp','.hpp') } |
        Select-Object -ExpandProperty FullName
})
$arguments = @('--style=file', '--fallback-style=none')
if ($Check) { $arguments += @('--dry-run','--Werror') } else { $arguments += '-i' }
& $ClangFormat @arguments @files
if ($LASTEXITCODE -ne 0) { throw 'C++ formatting check or update failed.' }

# clang-format handles layout. Check naming and constexpr declarations with
# comments and literals masked to preserve source locations.
$commentsAndLiterals = @'
//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\[\s\S]|[^"\\])*"|'(?:\\[\s\S]|[^'\\])*'
'@
$namespacePattern = '\bnamespace\s+(?<name>[A-Za-z_]\w*(?:\s*::\s*[A-Za-z_]\w*)*)\s*(?=[{=;])'
$constantPattern = '(?m)^[ \t]*(?:(?:inline|static)\s+)*constexpr\s+[\w:]+(?:\s+\w+)*?(?:<[^;{}]+?>)?[\s*&]+(?<name>[A-Za-z_]\w*)\s*(?=[={\[;])'
# Skip nested expressions and aggregate initializers when looking for a
# comma followed by another declarator.
$group = '\((?>[^()]|\((?<depth>)|\)(?<-depth>))*(?(depth)(?!))\)|\{(?>[^{}]|\{(?<depth>)|\}(?<-depth>))*(?(depth)(?!))\}|\[(?>[^\[\]]|\[(?<depth>)|\](?<-depth>))*(?(depth)(?!))\]'
$nextName = '\s*[*&]*\s*\w+\s*(?=[={\[;,])'
$extraDeclarator = [regex]::new("\G(?:$group|[^;(){}\[\],]|,(?!$nextName))*,$nextName")
$violations = foreach ($file in $files) {
    $source = [regex]::Replace([IO.File]::ReadAllText($file), $commentsAndLiterals, {
        param($match)
        $match.Value -replace '[^\r\n]', ' '
    })
    foreach ($match in [regex]::Matches($source, $namespacePattern)) {
        $name = $match.Groups['name'].Value
        if ($name -split '\s*::\s*' -cnotmatch '^[A-Z][A-Za-z0-9]*$') {
            $line = 1 + [regex]::Matches($source.Substring(0, $match.Index), '\n').Count
            "$($file):$($line): Use PascalCase for each component of namespace '$name'."
        }
    }
    foreach ($match in [regex]::Matches($source, $constantPattern)) {
        $name = $match.Groups['name'].Value
        $line = 1 + [regex]::Matches($source.Substring(0, $match.Index), '\n').Count
        if ($name -cnotmatch '^[A-Z][A-Z0-9]*(?:_[A-Z0-9]+)*$') {
            "$($file):$($line): Use UPPER_SNAKE_CASE for constant '$name'."
        }
        if ($extraDeclarator.IsMatch($source, $match.Index + $match.Length)) {
            "$($file):$($line): Declare each constexpr constant on its own line."
        }
    }
}
if ($violations) { throw ($violations -join [Environment]::NewLine) }
Write-Output "$(if ($Check) { 'Checked' } else { 'Formatted' }) $($files.Count) C++ files with $version"
