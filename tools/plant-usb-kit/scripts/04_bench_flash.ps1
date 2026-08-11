<#
    BENCH FLASH -- put the latest published firmware on a BENCH unit

    Flashes whatever 05_check_repo.ps1 last pulled into artifactsench\.
    That firmware is CI-built and has NOT run on hardware. This script exists
    to change that, on a unit whose failure costs nothing.

    It flashes the application at 0x10000 only. NVS and the SPIFFS queue are
    preserved, exactly as on the plant path -- a bench unit that is already
    provisioned keeps its settings.

    SAFETY: before writing anything it reads the connected device's own
    device_id off the serial console and refuses if that ID appears in
    known-live-devices.txt. A bench procedure must never land on a machine
    that is metering real production.

    Usage:
      .\04_bench_flash.ps1                      # oil-flow build (default)
      .\04_bench_flash.ps1 -Product mikiwire    # Miki Wire build
      .\04_bench_flash.ps1 -ComPort COM7
#>
[CmdletBinding()]
param(
    [ValidateSet("oilflow", "mikiwire")][string]$Product = "oilflow",
    [string]$ComPort = "",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$kitRoot     = Split-Path -Parent $PSScriptRoot
$benchDir    = Join-Path $kitRoot "artifacts\bench"
$evidenceDir = Join-Path $kitRoot "evidence"
$liveList    = Join-Path $kitRoot "known-live-devices.txt"
if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir | Out-Null }

# No version numbers and no hashes are hardcoded here, on purpose. The image
# is DISCOVERED by product prefix in artifacts\bench\, so whatever
# 05_check_repo.ps1 last pulled down is what gets flashed -- no code edit
# needed when a new release is published. And each image is verified against
# the .sha256 that CI generated beside it, because a hash pasted into a script
# is one more thing to fall out of step with the binary it describes.
$catalog = @{
    "oilflow"  = @{
        Prefix   = "covio-oilflow"
        HwCompat = "covio-oilflow-v1"
        Backend  = "https://data.funtastik.co.in"
        Board    = "ESP32-S3-Relay-1CH, pulse on GPIO1"
    }
    "mikiwire" = @{
        Prefix   = "miki-wire"
        HwCompat = "miki-wire-v1"
        Backend  = "https://compliance.mikigroup.co.in"
        Board    = "ESP32-S3-POE-ETH-8DI-8DO, pulse on GPIO4 (DI1)"
    }
}
$sel   = $catalog[$Product]
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

$benchVersion = "unknown"
$versionFile  = Join-Path $benchDir "VERSION.txt"
if (Test-Path $versionFile) { $benchVersion = (Get-Content $versionFile -Raw).Trim() }

$found = @(Get-ChildItem -Path $benchDir -Filter "$($sel.Prefix)-v*-app.bin" -ErrorAction SilentlyContinue)
if ($found.Count -eq 0) {
    Write-Host ""
    Write-Host "No $($sel.Prefix) image found in artifacts\bench\." -ForegroundColor Red
    Write-Host "Run .\05_check_repo.ps1 first to fetch the latest published firmware." -ForegroundColor Red
    exit 1
}
if ($found.Count -gt 1) {
    Write-Host ""
    Write-Host "More than one $($sel.Prefix) image is present in artifacts\bench\:" -ForegroundColor Red
    foreach ($f in $found) { Write-Host "    $($f.Name)" -ForegroundColor Red }
    Write-Host "Refusing to guess which one you meant. Leave exactly one, or re-run" -ForegroundColor Red
    Write-Host ".\05_check_repo.ps1 which replaces the folder contents cleanly." -ForegroundColor Red
    exit 1
}
$image = $found[0].FullName

Write-Host ""
Write-Host "=== COVIO BENCH FLASH -- v$benchVersion ($Product) ===" -ForegroundColor Cyan
Write-Host "  image   : $(Split-Path -Leaf $image)"
Write-Host "  hw_compat: $($sel.HwCompat)"
Write-Host "  backend : $($sel.Backend)"
Write-Host "  board   : $($sel.Board)"
Write-Host ""

# ------------------------------------------------------------------ the image
if (-not (Test-Path $image)) {
    Write-Host "MISSING: $image" -ForegroundColor Red
    Write-Host "Run ._check_repo.ps1 to fetch it. Nothing was written." -ForegroundColor Red
    exit 1
}
$shaFile = "$image.sha256"
if (-not (Test-Path $shaFile)) {
    Write-Host "MISSING CHECKSUM FILE: $shaFile" -ForegroundColor Red
    Write-Host "Without it the image cannot be verified, so it will not be flashed." -ForegroundColor Red
    Write-Host "Re-run ._check_repo.ps1, which always fetches both together." -ForegroundColor Red
    exit 1
}
$expected = ((Get-Content $shaFile -Raw) -split '\s+')[0].Trim().ToUpper()
$actual = (Get-FileHash -Path $image -Algorithm SHA256).Hash.ToUpper()
if ($actual -ne $expected) {
    Write-Host "CHECKSUM MISMATCH -- nothing was written." -ForegroundColor Red
    Write-Host "  expected $expected" -ForegroundColor Red
    Write-Host "  actual   $actual" -ForegroundColor Red
    exit 1
}
Write-Host "[OK] image checksum verified" -ForegroundColor Green

# ------------------------------------------------------------------ esptool
try { & python -m esptool version *> $null } catch { }
if ($LASTEXITCODE -ne 0) {
    Write-Host "esptool not available. Run .\00_setup.ps1 -InstallEsptool first." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] esptool available" -ForegroundColor Green

# ---------------------------------------------------------------- find the port
if ([string]::IsNullOrWhiteSpace($ComPort)) {
    $candidates = @(Get-WmiObject Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like "*VID_303A*" -and $_.Name -match '\(COM\d+\)' })
    if ($candidates.Count -eq 1 -and $candidates[0].Name -match '\((COM\d+)\)') {
        $ComPort = $matches[1]
        Write-Host "[OK] device found on $ComPort" -ForegroundColor Green
    } else {
        Write-Host "Could not identify the port automatically. Re-run with -ComPort COMx" -ForegroundColor Red
        exit 1
    }
}

# ------------------------------------------- WHICH DEVICE IS THIS, REALLY?
# Read the device's own identity before touching it. This is the guard that
# keeps a bench procedure off a production machine.
Write-Host ""
Write-Host "Reading the device's identity before writing anything..." -ForegroundColor Cyan
$deviceId = $null
$before   = ""
$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.ReadTimeout = 500
try {
    $port.Open()
    Start-Sleep -Milliseconds 400
    $port.WriteLine("show")
    Start-Sleep -Seconds 3
    $before = $port.ReadExisting()
    foreach ($line in ($before -split "`r?`n")) {
        if ($line -match '^\s*device_id\s*:\s*(\S+)') { $deviceId = $matches[1].Trim() }
    }
} catch {
    Write-Host "Could not open the serial port to identify the device: $($_.Exception.Message)" -ForegroundColor Yellow
} finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}
$before | Out-File -FilePath (Join-Path $evidenceDir "bench-pre-flash-$stamp.log") -Encoding utf8

$liveIds = @()
if (Test-Path $liveList) {
    $liveIds = @(Get-Content $liveList | ForEach-Object { $_.Trim() } |
                 Where-Object { $_ -ne "" -and -not $_.StartsWith("#") })
}

if ($deviceId) {
    Write-Host "  device_id: $deviceId"
    if ($liveIds -contains $deviceId) {
        Write-Host ""
        Write-Host "STOP. THIS IS A LIVE PRODUCTION DEVICE." -ForegroundColor Red
        Write-Host "$deviceId is listed in known-live-devices.txt." -ForegroundColor Red
        Write-Host "This script is for bench units only. Nothing was written." -ForegroundColor Red
        Write-Host "Flashing a production machine is a separate, authorized procedure." -ForegroundColor Red
        exit 1
    }
    Write-Host "  [OK] not in known-live-devices.txt" -ForegroundColor Green
} else {
    Write-Host "  Could not read device_id from the console." -ForegroundColor Yellow
    Write-Host "  The known-live-device check could NOT run." -ForegroundColor Yellow
}

# The Balaji meter's ID is not recorded yet, so the list alone cannot prove
# this is a bench unit. Ask the human, and say plainly why.
Write-Host ""
Write-Host "About to write v$benchVersion to the device on $ComPort." -ForegroundColor Yellow
Write-Host "This build has never run on hardware. It changes real runtime behaviour:" -ForegroundColor Yellow
Write-Host "  task watchdog, AP-mode metering, queue rollover, push/OTA backoff." -ForegroundColor Yellow
Write-Host ""
Write-Host "Confirm this is a BENCH unit and not a machine in production." -ForegroundColor Yellow
$confirm = Read-Host "Type BENCH to proceed, anything else to abort"
if ($confirm -ne "BENCH") {
    Write-Host "Aborted. Nothing was written." -ForegroundColor Cyan
    exit 0
}

# ------------------------------------------------------------------ flash
$flashLog = Join-Path $evidenceDir "bench-flash-$stamp.log"
Write-Host ""
Write-Host "Writing the application at 0x10000 ..." -ForegroundColor Cyan
& python -m esptool --chip esp32s3 --port $ComPort --baud $Baud `
    --before default_reset --after hard_reset `
    write_flash --verify 0x10000 $image 2>&1 | Tee-Object -FilePath $flashLog
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "WRITE FAILED (exit $LASTEXITCODE). See $flashLog" -ForegroundColor Red
    Write-Host "The device may be mid-write. Power-cycle it and re-run before doing anything else." -ForegroundColor Yellow
    exit 2
}
Write-Host "[OK] written and verified" -ForegroundColor Green

# ------------------------------------------------------------------ prove it
Write-Host ""
Write-Host "Waiting for boot, then reading it back..." -ForegroundColor Cyan
Start-Sleep -Seconds 6
$after = ""
$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.ReadTimeout = 500
try {
    $port.Open()
    Start-Sleep -Seconds 3
    $port.WriteLine("show")
    Start-Sleep -Seconds 3
    $after = $port.ReadExisting()
} catch {
    Write-Host "Could not reopen the port to verify: $($_.Exception.Message)" -ForegroundColor Yellow
} finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}
$after | Out-File -FilePath (Join-Path $evidenceDir "bench-post-flash-$stamp.log") -Encoding utf8

Write-Host ""
Write-Host $after
Write-Host ""
if ($after -match ('fw\s*:\s*' + [regex]::Escape($benchVersion))) {
    Write-Host "CONFIRMED: the device is running $benchVersion." -ForegroundColor Green
    Write-Host ""
    Write-Host "Next: work through BENCH-TESTING.md. The point of this flash is to" -ForegroundColor Cyan
    Write-Host "find out whether the new behaviour actually works on real hardware." -ForegroundColor Cyan
} else {
    Write-Host "Could not confirm v$benchVersion from the output above." -ForegroundColor Yellow
    Write-Host "Run .\01_capture.ps1 and read the boot banner." -ForegroundColor Yellow
}
Write-Host ""
Write-Host "Logs: $flashLog"
