param([ValidateSet('Debug','Release')][string]$Configuration = 'Release')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (!$vs) { throw 'Install Visual Studio Desktop development with C++ and the Windows SDK.' }
$cmake = Join-Path $vs 'Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = Join-Path (Split-Path $cmake) 'ctest.exe'
function Invoke-BuildTool([string]$Tool, [string[]]$ToolArguments) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Tool
    $start.WorkingDirectory = $projectRoot
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($arg in $ToolArguments) { $start.ArgumentList.Add($arg) }
    # Some hosts supply both Path and PATH; MSBuild rejects such an environment.
    $start.Environment.Clear()
    foreach ($entry in [Environment]::GetEnvironmentVariables().GetEnumerator()) {
        $start.Environment[$entry.Key.ToUpperInvariant()] = $entry.Value
    }
    $process = [System.Diagnostics.Process]::Start($start)
    $stdout = $process.StandardOutput.ReadToEndAsync()
    $stderr = $process.StandardError.ReadToEndAsync()
    $process.WaitForExit()
    Write-Output $stdout.GetAwaiter().GetResult()
    Write-Output $stderr.GetAwaiter().GetResult()
    if ($process.ExitCode) { throw "$Tool failed with exit code $($process.ExitCode)" }
}
$generator = if ($vs -match '\\18\\') { 'Visual Studio 18 2026' } else { 'Visual Studio 17 2022' }
Invoke-BuildTool $cmake @('-S', '.', '-B', 'build', '-G', $generator, '-A', 'x64')
Invoke-BuildTool $cmake @('--build', 'build', '--config', $Configuration, '--parallel')
Invoke-BuildTool $ctest @('--test-dir', 'build', '-C', $Configuration, '--output-on-failure')
