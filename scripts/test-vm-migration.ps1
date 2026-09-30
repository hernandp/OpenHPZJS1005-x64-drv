#requires -Version 7.0
#requires -RunAsAdministrator
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string]$CandidatePath,
    [Parameter(Mandatory)][string]$LegacyFiles,
    [Parameter(Mandatory)][string]$QueueTestPath,
    [Parameter(Mandatory)][string]$VcRuntimeInstaller
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$lab=Join-Path $root 'artifacts\OpenZJS-Win11-25H2'
$vm=Get-VM -Id '313693d0-ac6f-4d9e-80df-746e5980d194'
if ($vm.Name -ne 'OpenZJS-Win11-25H2' -or @(Get-VMNetworkAdapter -VM $vm | Where-Object SwitchName).Count) { throw 'Expected the dedicated isolated VM.' }
foreach($entry in (Get-Content (Join-Path $CandidatePath 'SHA256-MANIFEST.json') -Raw | ConvertFrom-Json)) {
    $path=[IO.Path]::GetFullPath((Join-Path $CandidatePath $entry.path))
    if (!$path.StartsWith([IO.Path]::GetFullPath($CandidatePath).TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)) { throw 'Candidate path escape.' }
    if ((Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw 'Candidate hash mismatch.' }
}
$legacyNames=@('lj1005.gpd','lj1005.ini','lj1005render.dll','lj1005_config.gpd','lj1005monitor.dll','lj1005-firmware.img')
$manifest=Get-Content (Join-Path $LegacyFiles 'manifest.json') -Raw | ConvertFrom-Json
if (@($manifest).Count -ne 6 -or @($manifest.file | Sort-Object -Unique).Count -ne 6) { throw 'Incomplete legacy manifest.' }
foreach($entry in $manifest) {
    if ($entry.file -notin $legacyNames -or (Get-FileHash -LiteralPath (Join-Path $LegacyFiles $entry.file)).Hash -ne $entry.sha256) { throw 'Legacy fixture hash mismatch.' }
}
if ((Get-FileHash (Join-Path $LegacyFiles 'lj1005-firmware.img')).Hash -ne '638B9AD4A2CF6888208926EF7C7DE073DA8BA4CDED25D31000A585C4526A9CA1') { throw 'Unexpected firmware.' }
$run=Join-Path $lab ('migration-'+[DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $run | Out-Null
$fixtureSnapshot=Join-Path $run 'harness'
New-Item -ItemType Directory -Path $fixtureSnapshot | Out-Null
Copy-Item -LiteralPath $PSCommandPath,(Join-Path $root 'tests\vm\migration-native.ps1'),(Join-Path $root 'tests\vm\migration-fixture.ps1') -Destination $fixtureSnapshot
$fixturePrefix=(Get-Content (Join-Path $fixtureSnapshot 'migration-native.ps1') -Raw)+[Environment]::NewLine+
    (Get-Content (Join-Path $fixtureSnapshot 'migration-fixture.ps1') -Raw)+[Environment]::NewLine
Get-ChildItem -LiteralPath $fixtureSnapshot -File | Get-FileHash | Select-Object Path,Hash | ConvertTo-Json | Set-Content (Join-Path $run 'harness-hashes.json')
$run | Set-Content (Join-Path $lab 'last-migration-run.txt')
$credential=Import-Clixml (Join-Path $lab 'credential.clixml')
$session=$null
$restarts=0
$signature=Get-AuthenticodeSignature -LiteralPath $VcRuntimeInstaller
if ($signature.Status -ne 'Valid' -or $signature.SignerCertificate.Subject -notmatch 'O=Microsoft Corporation') { throw 'Expected a Microsoft-signed VC++ redistributable.' }
$runtimeHash=(Get-FileHash -LiteralPath $VcRuntimeInstaller).Hash
function Phase([string]$Message) {
    $line=[DateTime]::UtcNow.ToString('o')+' '+$Message
    $line | Add-Content (Join-Path $run 'progress.log')
    Write-Host $line
}
function Connect-Guest {
    $deadline=(Get-Date).AddMinutes(7)
    do { try { return New-PSSession -VMId $vm.Id -Credential $credential } catch { Start-Sleep -Seconds 3 } } while ((Get-Date) -lt $deadline)
    throw 'Guest connection timed out.'
}
function Guest([scriptblock]$Work) {
    # Submit our checked-in fixture as PowerShell Direct commands, like the other
    # VM harnesses; do not change the guest's script execution policy.
    Invoke-Command -Session $script:session -ScriptBlock ([scriptblock]::Create($fixturePrefix+$Work.ToString()))
}
function Restart-Guest {
    $script:restarts++
    Phase 'Restarting only the disposable guest.'
    $boot=Invoke-Command -Session $script:session -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
    Invoke-Command -Session $script:session -ScriptBlock { & shutdown.exe /r /t 1; if ($LASTEXITCODE) { throw 'Restart request failed.' } }
    Remove-PSSession $script:session
    $script:session=$null
    $deadline=(Get-Date).AddMinutes(7)
    do {
        Start-Sleep -Seconds 5
        $connection=$null
        try {
            $connection=Connect-Guest
            $current=Invoke-Command -Session $connection -ScriptBlock { (Get-CimInstance Win32_OperatingSystem).LastBootUpTime }
            if ($current -gt $boot) { $script:session=$connection; return }
        } catch {}
        if ($connection) { Remove-PSSession $connection }
    } while ((Get-Date) -lt $deadline)
    throw 'Guest restart timed out.'
}
function Remove-Owned {
    for($attempt=1;$attempt -le 5;$attempt++) {
        $result=Guest {
            $code=Run-Setup 'remove-owned' '--quiet --remove'
            @{code=$code;message=(Get-Content "$base\results\remove-owned-error.txt" -Raw);state=(Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005' -ErrorAction SilentlyContinue).Status}
        }
        if ($result.code -eq 0) { return }
        if ($result.code -eq 3010 -or ($result.code -eq 1 -and $result.message -match 'Windows error: 3008' -and $result.state -eq 'Removing')) { Restart-Guest; continue }
        throw "Owned removal failed: $($result.message)"
    }
    throw 'Owned removal exceeded retry limit.'
}
function Retire-Legacy {
    for($attempt=1;$attempt -le 5;$attempt++) {
        $result=Guest { try { Remove-Legacy; 'removed' } catch { if ($_.Exception.Message -like 'RESTART_REQUIRED:*') { $_.Exception.Message } else { throw } } }
        if ($result -eq 'removed') { return }
        Phase $result
        Restart-Guest
    }
    throw 'Legacy removal exceeded retry limit.'
}
try {
    Phase 'Restoring the dedicated clean checkpoint.'
    if ($vm.State -ne 'Off') { Stop-VM -VM $vm -TurnOff -Force }
    Restore-VMSnapshot -VMSnapshot (Get-VMSnapshot -VM $vm -Name 'Clean-Windows-before-driver') -Confirm:$false
    if ((Get-VM -Id $vm.Id).State -ne 'Running') { Start-VM -VM $vm }
    $session=Connect-Guest
    Invoke-Command -Session $session -ScriptBlock {
        if ($env:COMPUTERNAME -ne 'OPENZJS-LAB' -or !(Confirm-SecureBootUEFI)) { throw 'Wrong guest baseline.' }
        New-Item -ItemType Directory -Path 'C:\OpenZJS-Migration-Test\results','C:\OpenZJS-Migration-Test\captures','C:\OpenZJS-Migration-Test\inbox' | Out-Null
        'Dedicated migration rehearsal' | Set-Content 'C:\OpenZJS-Migration-Test\lab-only.txt'
    }
    Copy-Item -LiteralPath (Resolve-Path $CandidatePath).Path -Destination 'C:\OpenZJS-Migration-Test\package' -Recurse -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $LegacyFiles).Path -Destination 'C:\OpenZJS-Migration-Test\legacy' -Recurse -ToSession $session
    Copy-Item -LiteralPath (Join-Path $fixtureSnapshot 'migration-fixture.ps1'),(Join-Path $fixtureSnapshot 'migration-native.ps1') -Destination 'C:\OpenZJS-Migration-Test' -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $QueueTestPath).Path -Destination 'C:\OpenZJS-Migration-Test\queue_test.exe' -ToSession $session
    Copy-Item -LiteralPath (Resolve-Path $VcRuntimeInstaller).Path -Destination 'C:\OpenZJS-Migration-Test\vc_redist.x64.exe' -ToSession $session
    $guestRuntimeHash=Invoke-Command -Session $session -ScriptBlock { (Get-FileHash 'C:\OpenZJS-Migration-Test\vc_redist.x64.exe').Hash }
    if ($guestRuntimeHash -ne $runtimeHash) { throw 'Copied runtime hash mismatch.' }
    @{sha256=$runtimeHash;signer=$signature.SignerCertificate.Subject} | ConvertTo-Json | Set-Content (Join-Path $run 'runtime.json')
    Phase 'Bootstrapping Windows core files through a guest-trusted package.'
    Guest {
        Get-Printer -Name 'Microsoft Print to PDF' -Full | Select-Object Name,DriverName,PortName,PermissionSDDL | ConvertTo-Json | Set-Content "$base\results\pdf-before.json"
        $cert=New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=OpenZJS Migration VM Lab Only' -CertStoreLocation Cert:\CurrentUser\My -HashAlgorithm SHA256 -NotAfter (Get-Date).AddMonths(3)
        Export-Certificate -Cert $cert -FilePath "$base\lab.cer" | Out-Null
        foreach($store in @('Root','TrustedPublisher')) { & certutil.exe -addstore -f $store "$base\lab.cer" > "$base\results\trust-$store.txt"; if($LASTEXITCODE){throw 'Guest trust failed.'} }
        if ((Set-AuthenticodeSignature "$base\package\driver\lj1005.cat" -Certificate $cert -HashAlgorithm SHA256).Status -ne 'Valid') { throw 'Guest signing failed.' }
        if ((Run-Setup 'bootstrap' "--quiet --install --offline --firmware $base\legacy\lj1005-firmware.img") -ne 0) { throw 'Bootstrap install failed.' }
        foreach($name in @('UNIDRV.DLL','UNIDRVUI.DLL','UNIDRV.HLP','UNIRES.DLL','STDNAMES.GPD')) { Copy-Item -LiteralPath "$spool\$name" -Destination "$base\inbox\$name" }
    }
    Remove-Owned
    Phase 'Installing the legacy build runtime dependency only inside the VM.'
    $runtimeExit=Guest {
        $process=Start-Process "$base\vc_redist.x64.exe" -ArgumentList "/install /quiet /norestart /log $base\results\vc-runtime.log" -Wait -PassThru -WindowStyle Hidden
        return $process.ExitCode
    }
    if ($runtimeExit -eq 3010) { Restart-Guest } elseif ($runtimeExit -ne 0) { throw "Runtime installation failed: $runtimeExit" }
    Phase 'Recreating the actual host development binaries and unused diagnostic registration.'
    Guest {
        Install-Legacy
        Set-Printer -Name $legacyQueue -Comment 'Migration preservation check' -Location 'VM lab' -Priority 7
        Set-PrintConfiguration -PrinterName $legacyQueue -PaperSize Letter
        Save-Settings
        Assert-Installation 'legacy' 'baseline'
        & "$base\queue_test.exe" $legacyQueue "$base\captures\before-portrait.zjs" portrait
        if ($LASTEXITCODE) { throw 'Baseline capture failed.' }
        if ((Run-Setup 'refuse-implicit-adoption' "--quiet --install --offline --firmware $base\legacy\lj1005-firmware.img") -ne 1) { throw 'Setup adopted the unowned installation.' }
        Assert-Installation 'legacy' 'refusal-preserved'
        Add-Printer -Name 'Migration dependent fixture' -DriverName $model -PortName 'FILE:'
        $rejected=$false
        try { Assert-LegacyRemovalSafe } catch { if ($_.Exception.Message -match 'dependent queue') { $rejected=$true } else { throw } }
        if (!$rejected) { throw 'Dependent legacy queue was accepted.' }
        Remove-Printer -Name 'Migration dependent fixture'
        Assert-Installation 'legacy' 'dependency-refusal-preserved'
        Copy-Item "$base\saved\mode.bin" "$base\mode-original.bin"
        [IO.File]::WriteAllBytes("$base\saved\mode.bin",[byte[]](1,2,3))
        $rejected=$false
        try { Assert-LegacyRemovalSafe } catch { if ($_.Exception.Message -match 'settings hash mismatch') { $rejected=$true } else { throw } }
        if (!$rejected) { throw 'Corrupt recovery settings were accepted.' }
        Copy-Item "$base\mode-original.bin" "$base\saved\mode.bin" -Force
        Copy-Item "$base\package" "$base\tampered" -Recurse
        Add-Content "$base\tampered\driver\lj1005.gpd" '*% altered migration source'
        & pnputil.exe /add-driver "$base\tampered\driver\lj1005.inf" > "$base\results\tampered-stage.txt"
        if ($LASTEXITCODE -eq 0) { throw 'Tampered package staged.' }
        Assert-Installation 'legacy' 'tampered-source-preserved'
        & pnputil.exe /add-driver "$base\package\driver\lj1005.inf" > "$base\results\stage.txt"
        if ($LASTEXITCODE -ne 0) { throw 'Trusted target did not stage before removal.' }
    }
    Phase 'Retiring the legacy resources, then testing recovery from failed fresh installation.'
    Retire-Legacy
    Guest {
        [IO.File]::WriteAllBytes("$base\invalid.img",[byte[]](1,2,3))
        if ((Run-Setup 'failure-after-retirement' "--quiet --install --offline --firmware $base\invalid.img") -ne 1) { throw 'Expected install failure missing.' }
        Install-Legacy
        Assert-Installation 'legacy' 'failed-install-recovery'
    }
    Phase 'Migrating to the packaged driver with restored global settings.'
    Retire-Legacy
    Guest {
        if ((Run-Setup 'migration-install' "--quiet --install --offline --firmware $base\legacy\lj1005-firmware.img") -ne 0) { throw 'Migration install failed.' }
        Restore-Settings $newQueue
        Assert-Installation 'owned' 'migration-success'
        & "$base\queue_test.exe" $newQueue "$base\captures\migrated-portrait.zjs" portrait
        if ($LASTEXITCODE) { throw 'Migrated capture failed.' }
    }
    Phase 'Removing the candidate and restoring the original development build.'
    Remove-Owned
    Guest {
        Install-Legacy
        Assert-Installation 'legacy' 'post-success-recovery'
        & "$base\queue_test.exe" $legacyQueue "$base\captures\restored-portrait.zjs" portrait
        if ($LASTEXITCODE) { throw 'Restored capture failed.' }
        if (!(Confirm-SecureBootUEFI)) { throw 'Secure Boot changed.' }
    }
    @{status='passed';candidate=$CandidatePath;legacyFiles=$LegacyFiles;guestRestarts=$restarts;physicalUsb='not tested';hostInstallation='unchanged';scope='VM-only explicit replacement and legacy restoration'} | ConvertTo-Json | Set-Content (Join-Path $run 'result.json')
    Phase 'Migration rehearsal passed; independent capture verification remains.'
} catch {
    $_ | Out-String | Set-Content (Join-Path $run 'failure.txt')
    throw
} finally {
    if ($session) {
        try { Copy-Item -Path 'C:\OpenZJS-Migration-Test\results','C:\OpenZJS-Migration-Test\captures' -Destination $run -Recurse -FromSession $session }
        finally { Remove-PSSession $session }
    }
}
