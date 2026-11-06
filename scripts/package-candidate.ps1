#requires -Version 7.0
[CmdletBinding()]
param(
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10",
    [string]$WdkVersion = '10.0.26100.0',
    [string]$CatalogOS = '10_GE_X64',
    [string]$OutputPathFile
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$projectRoot = Split-Path $PSScriptRoot -Parent
$infVerif = Join-Path $WdkRoot "tools\$WdkVersion\x64\infverif.exe"
$gpdCheck = Join-Path $WdkRoot "tools\$WdkVersion\x64\gpdcheck.exe"
foreach ($tool in @($infVerif, $gpdCheck)) {
    if (!(Test-Path -LiteralPath $tool -PathType Leaf)) { throw "Required WDK tool missing: $tool" }
}
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or !$vs) { throw 'Visual Studio C++ tools are required.' }
$msvc = Get-ChildItem -LiteralPath (Join-Path $vs 'VC\Tools\MSVC') -Directory |
    Sort-Object { [version]$_.Name } -Descending | Select-Object -First 1
$dumpbin = Join-Path $msvc.FullName 'bin\Hostx64\x64\dumpbin.exe'
if (!(Test-Path -LiteralPath $dumpbin)) { throw "Missing $dumpbin" }

# A unique directory avoids reusing stale objects or overwriting a previous candidate.
$candidate = Join-Path $projectRoot ('artifacts\rc-' + [guid]::NewGuid().ToString('N').Substring(0, 8))
$source = Join-Path $candidate 'source'
$bundle = Join-Path $candidate 'HPLJ1005x64'
$payload = Join-Path $bundle 'driver'
$logs = Join-Path $candidate 'validation'
New-Item -ItemType Directory -Path $source, $payload, $logs | Out-Null

function Write-Manifest([string]$Directory) {
    $entries = Get-ChildItem -LiteralPath $Directory -Recurse -File | Where-Object Name -ne 'SHA256-MANIFEST.json' |
        ForEach-Object {
            [ordered]@{
                path = [IO.Path]::GetRelativePath($Directory, $_.FullName).Replace('\', '/')
                sha256 = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
                bytes = $_.Length
            }
        }
    $entries | Sort-Object { $_.path } | ConvertTo-Json -Depth 4 |
        Set-Content -LiteralPath (Join-Path $Directory 'SHA256-MANIFEST.json') -Encoding utf8
}

function Invoke-Check([string]$Tool, [string[]]$ToolArguments, [string]$LogName) {
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Tool
    $start.WorkingDirectory = $logs
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in $ToolArguments) { $start.ArgumentList.Add($argument) }
    $start.Environment.Clear()
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $start.Environment[$entry.Key.ToUpperInvariant()] = $entry.Value
    }
    $process = [Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    $output = $stdout.GetAwaiter().GetResult() + $stderr.GetAwaiter().GetResult()
    $output | Set-Content -LiteralPath (Join-Path $logs $LogName) -Encoding utf8
    if ($process.ExitCode -ne 0) { throw "$LogName failed ($($process.ExitCode)). See $logs" }
    Write-Host "$LogName passed"
    return $output
}

# Snapshot only public source paths known to Git; ignored firmware, captures,
# downloaded SDK files and existing build outputs cannot enter the snapshot.
$sourcePaths = @('CMakeLists.txt', 'LICENSE', 'README.md', 'THIRD_PARTY.md', '.gitignore',
    '.gitattributes', '.clang-format', '.github', 'driver', 'include', 'src', 'scripts', 'tests', 'vendor', 'docs')
$files = & git -C $projectRoot -c core.quotepath=false ls-files --cached --others --exclude-standard -- @sourcePaths
if ($LASTEXITCODE -ne 0 -or !$files) { throw 'Source snapshot requires a Git checkout.' }
$allowedExtensions = @('.cpp', '.hpp', '.c', '.h', '.ps1', '.py', '.cmake', '.md', '.gpd', '.inf', '.ini', '.def', '.yml')
foreach ($relative in ($files | Sort-Object -Unique)) {
    if ($relative.Contains('..') -or [IO.Path]::IsPathRooted($relative)) { throw "Invalid source path: $relative" }
    if ($relative.Contains('/') -and [IO.Path]::GetExtension($relative) -notin $allowedExtensions -and
        $relative -ne 'driver/version.rc.in') {
        throw "Review unexpected source file before packaging: $relative"
    }
    $original = Get-Item -LiteralPath (Join-Path $projectRoot $relative)
    # OneDrive placeholders are also reparse points, but are ordinary source
    # files. Reject actual links/junctions, including any parent below the root.
    $component = $original
    while ($component.FullName -ne $projectRoot) {
        if ($component.LinkType) { throw "Source links are not allowed: $relative" }
        $component = Get-Item -LiteralPath (Split-Path $component.FullName -Parent)
    }
    $destination = Join-Path $source $relative
    New-Item -ItemType Directory -Force -Path (Split-Path $destination -Parent) | Out-Null
    Copy-Item -LiteralPath $original.FullName -Destination $destination
}
$revision = & git -C $projectRoot rev-parse HEAD
if ($LASTEXITCODE -ne 0) { throw 'Cannot identify source revision.' }
$dirty = @(& git -C $projectRoot status --porcelain).Count -ne 0
if ($LASTEXITCODE -ne 0) { throw 'Cannot inspect source status.' }
$versionMatch = [regex]::Match((Get-Content -LiteralPath (Join-Path $source 'CMakeLists.txt') -Raw),
    'project\(HPLJ1005x64 VERSION ([0-9]+\.[0-9]+\.[0-9]+)')
if (!$versionMatch.Success) { throw 'Cannot read project version.' }
$version = $versionMatch.Groups[1].Value
$infText = Get-Content -LiteralPath (Join-Path $source 'driver\lj1005.inf') -Raw
if ($infText -notmatch ('(?m)^DriverVer=[^,]+,' + [regex]::Escape($version) + '\.0\s*$')) {
    throw 'CMake and INF versions differ.'
}
Write-Manifest $source
$sourceZip = Join-Path $candidate "HPLJ1005x64-$version-source.zip"
Compress-Archive -LiteralPath $source -DestinationPath $sourceZip

# Build exactly the archived source, not binaries from the working checkout.
& (Join-Path $source 'scripts\build.ps1') -Configuration Release *>&1 |
    Tee-Object -FilePath (Join-Path $logs 'build-tests.log') | Out-Host
$binaryRoot = Join-Path $source 'build\Release'
foreach ($file in @('lj1005.inf', 'lj1005.gpd', 'lj1005.ini', 'lj1005render.dll', 'lj1005monitor.dll')) {
    Copy-Item -LiteralPath (Join-Path $binaryRoot $file) -Destination $payload
}
foreach ($file in @('LICENSE', 'THIRD_PARTY.md')) {
    Copy-Item -LiteralPath (Join-Path $source $file) -Destination $bundle
}
Copy-Item -LiteralPath (Join-Path $source 'docs\MINI_INSTALL.md') -Destination (Join-Path $bundle 'README.md')

& (Join-Path $source 'scripts\verify-binary-versions.ps1') -PackagePath $bundle -ExpectedVersion "$version.0" |
    ConvertTo-Json -Depth 3 | Set-Content -LiteralPath (Join-Path $logs 'binary-versions.json') -Encoding utf8

foreach ($file in (Get-ChildItem -LiteralPath $payload -Filter '*.dll')) {
    $bytes = [IO.File]::ReadAllBytes($file.FullName)
    $peOffset = [BitConverter]::ToInt32($bytes, 0x3c)
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x4550 -or
        [BitConverter]::ToUInt16($bytes, $peOffset + 4) -ne 0x8664) { throw "$($file.Name) is not x64 PE." }
    $dependencies = Invoke-Check $dumpbin @('/dependents', $file.FullName) ($file.Name + '-imports.log')
    if ($dependencies -match '(?im)^\s+(?:MSVCP\S*|VCRUNTIME\S*|MSVCR\S*|UCRTBASE[D]?)\.dll\s*$') {
        throw "$($file.Name) still depends on a separately installed C/C++ runtime."
    }
}
$null = Invoke-Check $infVerif @('/v', (Join-Path $payload 'lj1005.inf')) 'infverif.log'
$null = Invoke-Check $gpdCheck @('-n', '-x', (Join-Path $payload 'lj1005.gpd')) 'gpdcheck.log'
[ordered]@{
    version = $version
    baseCommit = $revision
    workingTreeModified = $dirty
    sourceArchive = [IO.Path]::GetFileName($sourceZip)
    sourceArchiveSha256 = (Get-FileHash -LiteralPath $sourceZip -Algorithm SHA256).Hash.ToLowerInvariant()
    createdUtc = [DateTime]::UtcNow.ToString('o')
    wdkVersion = $WdkVersion
    catalogOS = $CatalogOS
    packageState = 'unsigned-engineering-candidate'
    cleanMachineTested = $false
} | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $bundle 'BUILD-INFO.json') -Encoding utf8
Write-Manifest $bundle
$binaryZip = Join-Path $candidate "HPLJ1005x64-$version-unsigned-candidate.zip"
Compress-Archive -LiteralPath $bundle -DestinationPath $binaryZip
Get-FileHash -LiteralPath $sourceZip, $binaryZip -Algorithm SHA256 |
    ForEach-Object { $_.Hash.ToLowerInvariant() + '  ' + [IO.Path]::GetFileName($_.Path) } |
    Set-Content -LiteralPath (Join-Path $candidate 'SHA256SUMS.txt') -Encoding ascii
Write-Host "Candidate: $candidate"
Write-Host 'Unsigned engineering package only. Signing and clean-machine installation have NOT been verified.'
if ($OutputPathFile) { $candidate | Set-Content -LiteralPath $OutputPathFile -Encoding utf8 }
