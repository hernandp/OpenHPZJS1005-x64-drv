# VM-only rehearsal primitives; not a host migration tool.
$ErrorActionPreference='Stop'
if ($env:COMPUTERNAME -ne 'OPENZJS-LAB' -or !(Test-Path 'C:\OpenZJS-Migration-Test\lab-only.txt')) { throw 'Dedicated VM fixture only.' }
$base='C:\OpenZJS-Migration-Test'
$legacyQueue='HP LaserJet 1005 Open ZJS (development)'
$newQueue='HP LaserJet 1005 (Open ZJS)'
$model='HP LaserJet 1005 Series (Open ZJS)'
$diagnostic='HP LaserJet 1005 GPD diagnostic'
$monitor='HP LaserJet 1005 Firmware Monitor (development)'
$spool='C:\Windows\System32\spool\drivers\x64\3'
$driverKey='HKLM:\SYSTEM\CurrentControlSet\Control\Print\Environments\Windows x64\Drivers\Version-3'
function Assert-Backup {
    foreach ($entry in (Get-Content "$base\legacy\manifest.json" -Raw | ConvertFrom-Json)) {
        if ($entry.file -notin @('lj1005.gpd','lj1005.ini','lj1005render.dll','lj1005_config.gpd','lj1005monitor.dll','lj1005-firmware.img')) { throw 'Unknown legacy file.' }
        if ((Get-FileHash -LiteralPath "$base\legacy\$($entry.file)").Hash -ne $entry.sha256) { throw 'Legacy backup hash mismatch.' }
    }
    if (Test-Path "$base\saved\manifest.json") {
        foreach ($entry in (Get-Content "$base\saved\manifest.json" -Raw | ConvertFrom-Json)) {
            if ($entry.file -notin @('queue.json','mode.bin')) { throw 'Unknown settings file.' }
            if ((Get-FileHash -LiteralPath "$base\saved\$($entry.file)").Hash -ne $entry.sha256) { throw 'Saved queue settings hash mismatch.' }
        }
    }
}
function Run-Setup([string]$Label,[string]$Arguments,[string]$Package='package') {
    $process=Start-Process "$base\$Package\Setup.exe" -ArgumentList $Arguments -Wait -PassThru -WindowStyle Hidden -RedirectStandardError "$base\results\$Label-error.txt"
    @{label=$Label;exitCode=$process.ExitCode} | ConvertTo-Json -Compress | Add-Content "$base\results\steps.jsonl"
    return $process.ExitCode
}
function Save-Settings {
    New-Item -ItemType Directory -Path "$base\saved" | Out-Null
    Get-Printer -Name $legacyQueue -Full | Select-Object PortName,Comment,Location,Priority,Shared,ShareName,PermissionSDDL | ConvertTo-Json | Set-Content "$base\saved\queue.json"
    [MigrationLab]::SaveMode($legacyQueue,"$base\saved\mode.bin")
    @('queue.json','mode.bin') | ForEach-Object { @{file=$_;sha256=(Get-FileHash "$base\saved\$_").Hash} } | ConvertTo-Json | Set-Content "$base\saved\manifest.json"
}
function Restore-Settings([string]$Name) {
    Assert-Backup
    $settings=Get-Content "$base\saved\queue.json" -Raw | ConvertFrom-Json
    if ($settings.Shared) { throw 'Sharing is outside this rehearsal.' }
    Set-Printer -Name $Name -PortName $settings.PortName -Comment $settings.Comment -Location $settings.Location -Priority $settings.Priority -PermissionSDDL $settings.PermissionSDDL
    [MigrationLab]::RestoreMode($Name,"$base\saved\mode.bin")
}
function Install-Legacy {
    Assert-Backup
    if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Remove owned installation first.' }
    foreach ($name in @('UNIDRV.DLL','UNIDRVUI.DLL','UNIDRV.HLP','UNIRES.DLL','STDNAMES.GPD')) {
        if (!(Test-Path "$spool\$name")) { Copy-Item -LiteralPath "$base\inbox\$name" -Destination "$spool\$name" }
    }
    foreach ($name in @('lj1005monitor.dll','lj1005-firmware.img')) {
        if (Test-Path "C:\Windows\System32\$name") { throw 'Existing system destination; finish removal first.' }
        Copy-Item -LiteralPath "$base\legacy\$name" -Destination "C:\Windows\System32\$name"
    }
    [MigrationLab]::InstallMonitor()
    [MigrationLab]::InstallDriver("$base\legacy",$spool,$false)
    [MigrationLab]::InstallDriver("$base\legacy",$spool,$true)
    Add-Printer -Name $legacyQueue -DriverName $model -PortName 'FILE:'
    if (Test-Path "$base\saved\queue.json") { Restore-Settings $legacyQueue }
}
function Assert-LegacyRemovalSafe {
    Assert-Backup
    if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Owned Setup state exists.' }
    foreach ($printer in Get-Printer) {
        if ($printer.DriverName -in @($model,$diagnostic)) {
            if ($printer.Name -ne $legacyQueue -or $printer.DriverName -ne $model) { throw 'A dependent queue blocks migration.' }
            if (@(Get-PrintJob -PrinterName $printer.Name).Count) { throw 'Pending jobs block migration.' }
        }
    }
    foreach ($key in Get-ChildItem $driverKey) {
        $record=Get-ItemProperty $key.PSPath
        if ($key.PSChildName -in @($model,$diagnostic)) {
            if ($record.InfPath -or $record.Driver -ne 'UNIDRV.DLL' -or $record.'Configuration File' -ne 'UNIDRVUI.DLL') { throw 'Unknown driver registration.' }
            $data=if($key.PSChildName -eq $model){'lj1005.gpd'}else{'lj1005_config.gpd'}
            $expectedMonitor=if($key.PSChildName -eq $model){$monitor}else{''}
            if ($record.'Data File' -ne $data -or [string]$record.Monitor -ne $expectedMonitor) { throw 'Changed legacy registration.' }
        } elseif ($record.Monitor -eq $monitor -or @($record.'Dependent Files' | Where-Object { $_ -match '^lj1005' }).Count -or $record.'Data File' -match '^lj1005') { throw 'Another driver depends on legacy resources.' }
    }
    $registered=Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\$monitor" -ErrorAction SilentlyContinue
    if ($registered -and $registered.Driver -ne 'lj1005monitor.dll') { throw 'Unknown monitor registration.' }
    foreach ($entry in (Get-Content "$base\legacy\manifest.json" -Raw | ConvertFrom-Json)) {
        $path=if($entry.file -in @('lj1005monitor.dll','lj1005-firmware.img')){"C:\Windows\System32\$($entry.file)"}else{"$spool\$($entry.file)"}
        if ((Test-Path -LiteralPath $path) -and (Get-FileHash -LiteralPath $path).Hash -ne $entry.sha256) { throw 'Changed live legacy file.' }
    }
}
function Remove-Legacy {
    Assert-LegacyRemovalSafe
    if (Get-Printer -Name $legacyQueue -ErrorAction SilentlyContinue) { Remove-Printer -Name $legacyQueue }
    foreach ($name in @($diagnostic,$model)) {
        if (Test-Path "$driverKey\$name") {
            # Unregister without deleting shared files; remove verified private files below.
            if (![MigrationLab]::DeletePrinterDriverEx($null,'Windows x64',$name,0,0)) { throw ('RESTART_REQUIRED: driver removal '+[Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
        }
    }
    if (Test-Path "HKLM:\SYSTEM\CurrentControlSet\Control\Print\Monitors\$monitor") {
        if (![MigrationLab]::DeleteMonitor($null,'Windows x64',$monitor)) { throw ('RESTART_REQUIRED: monitor removal '+[Runtime.InteropServices.Marshal]::GetLastWin32Error()) }
    }
    foreach ($entry in (Get-Content "$base\legacy\manifest.json" -Raw | ConvertFrom-Json)) {
        $path=if($entry.file -in @('lj1005monitor.dll','lj1005-firmware.img')){"C:\Windows\System32\$($entry.file)"}else{"$spool\$($entry.file)"}
        if (Test-Path -LiteralPath $path) {
            # The original imported firmware can be read-only. These are only
            # the exact private files whose hashes were checked above.
            try { Remove-Item -LiteralPath $path -Force -ErrorAction Stop }
            catch {
                if (($_.Exception.HResult -band 0xffff) -in @(32,33)) {
                    throw "RESTART_REQUIRED: cannot remove $($entry.file): $($_.Exception.Message)"
                }
                throw
            }
        }
    }
}
function Assert-Installation([string]$Kind,[string]$Label) {
    $queue=if($Kind -eq 'legacy'){$legacyQueue}else{$newQueue}
    $folder=if($Kind -eq 'legacy'){"$base\legacy"}else{"$base\package\driver"}
    $settings=Get-Content "$base\saved\queue.json" -Raw | ConvertFrom-Json
    $actual=Get-Printer -Name $queue -Full | Select-Object PortName,Comment,Location,Priority,Shared,ShareName,PermissionSDDL
    if (($settings | ConvertTo-Json -Compress) -ne ($actual | ConvertTo-Json -Compress)) { throw 'Queue settings or permissions differ.' }
    if ((Get-PrintConfiguration -PrinterName $queue).PaperSize -ne 'Letter') { throw 'Paper defaults differ.' }
    foreach ($name in @('lj1005.gpd','lj1005.ini','lj1005render.dll')) {
        if ((Get-FileHash "$spool\$name").Hash -ne (Get-FileHash "$folder\$name").Hash) { throw "Live payload differs: $name" }
    }
    if ((Get-FileHash 'C:\Windows\System32\lj1005monitor.dll').Hash -ne (Get-FileHash "$folder\lj1005monitor.dll").Hash) { throw 'Monitor differs.' }
    if ((Get-FileHash 'C:\Windows\System32\lj1005-firmware.img').Hash -ne (Get-FileHash "$base\legacy\lj1005-firmware.img").Hash) { throw 'Firmware differs.' }
    $pdf=Get-Printer -Name 'Microsoft Print to PDF' -Full | Select-Object Name,DriverName,PortName,PermissionSDDL
    if (($pdf | ConvertTo-Json -Compress) -ne (Get-Content "$base\results\pdf-before.json" -Raw | ConvertFrom-Json | ConvertTo-Json -Compress)) { throw 'Unrelated PDF printer changed.' }
    if ($Kind -eq 'legacy') {
        if (Test-Path 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005') { throw 'Owned record remains.' }
        if (!(Get-PrinterDriver -Name $diagnostic)) { throw 'Diagnostic registration missing.' }
        if ((Get-FileHash "$spool\lj1005_config.gpd").Hash -ne (Get-FileHash "$base\legacy\lj1005_config.gpd").Hash) { throw 'Diagnostic GPD differs.' }
    } else {
        $state=Get-ItemProperty 'HKLM:\SOFTWARE\OpenZJS\LaserJet1005'
        if ($state.Status -ne 'Complete' -or $state.RepairSchema -ne '1') { throw 'Owned installation incomplete.' }
        if (Get-PrinterDriver -Name $diagnostic -ErrorAction SilentlyContinue) { throw 'Diagnostic registration remains after migration.' }
    }
    @{label=$Label;kind=$Kind;status='passed'} | ConvertTo-Json -Compress | Add-Content "$base\results\assertions.jsonl"
}
