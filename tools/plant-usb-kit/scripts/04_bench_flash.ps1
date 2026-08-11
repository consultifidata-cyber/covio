<#
    BENCH FLASH -- put the latest published firmware on a BENCH unit

    Flashes whatever 05_check_repo.ps1 last pulled into artifacts\bench\.
    That firmware is CI-built and has NOT run on hardware. This script exists
    to change that, on a unit whose failure costs nothing.

    It flashes the application at 0x10000 only. NVS and the SPIFFS queue are
    preserved, exactly as on the plant path -- a bench unit that is already
    provisioned keeps its settings.

    SAFETY: before writing anything it identifies the device from its eFuse
    MAC (read from silicon by esptool, reversed into the firmware's device_id
    form) and refuses if that ID appears in known-live-devices.txt. It also
    refuses if it cannot identify the device at all -- it will not ask a human
    to vouch for a board it could not name. A bench procedure must never land
    on a machine that is metering real production.

    Usage:
      .\04_bench_flash.ps1                      # oil-flow build (default)
      .\04_bench_flash.ps1 -Product mikiwire    # Miki Wire build
      .\04_bench_flash.ps1 -ComPort COM7
#>
[CmdletBinding()]
param(
    [ValidateSet("oilflow", "mikiwire")][string]$Product = "oilflow",
    [string]$ComPort = "",
    [int]$Baud = 460800,
    # First-time setup for a BLANK bench board: also writes the bootloader,
    # partition table and boot_app0, without which an app-only write leaves a
    # device that cannot boot. The script detects a blank board and tells you
    # to pass this; it is never applied silently.
    [switch]$Virgin
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
    Write-Host "Run .\05_check_repo.ps1 to fetch it. Nothing was written." -ForegroundColor Red
    exit 1
}
$shaFile = "$image.sha256"
if (-not (Test-Path $shaFile)) {
    Write-Host "MISSING CHECKSUM FILE: $shaFile" -ForegroundColor Red
    Write-Host "Without it the image cannot be verified, so it will not be flashed." -ForegroundColor Red
    Write-Host "Re-run .\05_check_repo.ps1, which always fetches both together." -ForegroundColor Red
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
# THE AUTHORITATIVE ANSWER IS THE eFUSE MAC, NOT THE CONSOLE.
#
# This guard once nearly failed on a live production meter. The board looked
# like a spare, its USB console returned zero bytes, so no device_id could be
# read -- and the script fell through to "type BENCH" with the operator
# believing it was a bench unit. It was the commissioned Balaji oil-flow meter,
# metering and pushing to the ERP at that moment.
#
# The console can be silent for reasons that have nothing to do with the board
# being blank. The eFuse MAC cannot: esptool reads it from silicon, on any
# board, in any state. So the MAC is now the identity of record.
#
# !! BYTE ORDER IS THE TRAP. esptool prints the MAC in wire order; the firmware
# builds device_id from ESP.getEfuseMac(), which loads those six bytes into a
# LITTLE-ENDIAN uint64 and formats "esp32-%04X%08X" (store.h:170). The result
# is the MAC fully reversed:
#     esptool  28:84:85:B2:E5:F4   ->   firmware  esp32-F4E5B2858428
# The same chip therefore looks like two different devices in the two tools.
# Comparing the un-reversed form against known-live-devices.txt would silently
# never match. Reverse, then compare.
function Get-CovioDeviceId {
    param([string]$Port, [int]$BaudRate)
    $out = & python -m esptool --chip esp32s3 --port $Port --baud $BaudRate read_mac 2>&1
    foreach ($line in $out) {
        if ("$line" -match 'MAC:\s*((?:[0-9a-fA-F]{2}:){5}[0-9a-fA-F]{2})') {
            $bytes = $matches[1].Split(':')
            [array]::Reverse($bytes)
            return "esp32-" + (($bytes -join '').ToUpper())
        }
    }
    return $null
}

Write-Host ""
Write-Host "Identifying the device from its eFuse MAC before writing anything..." -ForegroundColor Cyan
$deviceId  = Get-CovioDeviceId -Port $ComPort -BaudRate $Baud
$consoleId = $null
$before    = ""
$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
# DTR MUST BE ASSERTED. On an ESP32-S3 built with ARDUINO_USB_CDC_ON_BOOT=1,
# Serial is TinyUSB CDC, and that layer only transmits while the host asserts
# DTR -- with DTR low the device writes into a void and the host reads 0 bytes.
# An earlier version of this kit held DTR low to avoid resetting the device and
# got exactly that: a completely silent console on a working board. Asserting
# DTR may reboot the unit on connect; that is survivable (queue lives in SPIFFS,
# totalizer is checkpointed) and the boot banner it produces is useful. RTS
# stays low -- it is the RTS/DTR *sequence* that drives the reset logic.
$port.DtrEnable = $true
$port.RtsEnable = $false
$port.ReadTimeout = 500
try {
    $port.Open()
    Start-Sleep -Milliseconds 400
    $port.WriteLine("show")
    Start-Sleep -Seconds 3
    $before = $port.ReadExisting()
    foreach ($line in ($before -split "`r?`n")) {
        # Corroboration only. If the console does answer and disagrees with the
        # MAC, that is a contradiction worth stopping for, not averaging over.
        if ($line -match '^\s*device_id\s*:\s*(\S+)') { $consoleId = $matches[1].Trim() }
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

# FAIL CLOSED. If we cannot establish who this device is, we do not write to
# it. The previous version warned and then carried on to the human
# confirmation -- and a human who believes they are holding a spare will type
# BENCH. Identity is a machine's job, not a tired operator's.
if (-not $deviceId) {
    Write-Host ""
    Write-Host "CANNOT IDENTIFY THIS DEVICE - nothing was written." -ForegroundColor Red
    Write-Host ""
    Write-Host "esptool could not read the eFuse MAC, so there is no way to tell" -ForegroundColor Red
    Write-Host "whether this is a bench unit or a machine in production." -ForegroundColor Red
    Write-Host "This script will not guess, and it will not ask you to guess." -ForegroundColor Red
    Write-Host ""
    Write-Host "Check the cable and the port, then re-run. If esptool genuinely" -ForegroundColor Yellow
    Write-Host "cannot talk to the board, stop and escalate." -ForegroundColor Yellow
    exit 1
}

Write-Host "  device_id (from eFuse MAC): $deviceId" -ForegroundColor Green
if ($consoleId) {
    if ($consoleId -eq $deviceId) {
        Write-Host "  console agrees: $consoleId" -ForegroundColor Green
    } else {
        Write-Host ""
        Write-Host "CONTRADICTION - nothing was written." -ForegroundColor Red
        Write-Host "  eFuse MAC says : $deviceId" -ForegroundColor Red
        Write-Host "  console says   : $consoleId" -ForegroundColor Red
        Write-Host "Two different identities from one board. Stop and escalate." -ForegroundColor Red
        exit 1
    }
} else {
    Write-Host "  console did not answer - not a problem, the MAC is authoritative" -ForegroundColor DarkGray
}

if ($liveIds -contains $deviceId) {
    Write-Host ""
    Write-Host "STOP. THIS IS A LIVE PRODUCTION DEVICE." -ForegroundColor Red
    Write-Host "$deviceId is listed in known-live-devices.txt." -ForegroundColor Red
    Write-Host "This script is for bench units only. Nothing was written." -ForegroundColor Red
    Write-Host "Flashing a production machine is a separate, authorized procedure." -ForegroundColor Red
    exit 1
}
Write-Host "  [OK] not in known-live-devices.txt" -ForegroundColor Green

Write-Host ""
Write-Host "About to write v$benchVersion to $deviceId on $ComPort." -ForegroundColor Yellow
Write-Host "This build has never run on hardware. It changes real runtime behaviour:" -ForegroundColor Yellow
Write-Host "  task watchdog, AP-mode metering, queue rollover, push/OTA backoff." -ForegroundColor Yellow
Write-Host ""
Write-Host "The ID above is read from silicon and cannot be spoofed by a silent" -ForegroundColor Yellow
Write-Host "console. If you do not recognise it as a bench unit, abort." -ForegroundColor Yellow
$confirm = Read-Host "Type BENCH to proceed, anything else to abort"
if ($confirm -ne "BENCH") {
    Write-Host "Aborted. Nothing was written." -ForegroundColor Cyan
    exit 0
}

# ---------------------------------------------- is this board even bootable?
# Writing the app alone assumes a second-stage bootloader at 0x0 and a
# partition table at 0x8000 already exist. On a VIRGIN board they do not, and
# an app-only write produces a device that cannot boot at all -- with nothing
# on the console to explain why. Read the flash and find out rather than
# assuming. This resets the board into download mode, which is fine on a bench
# unit.
$flashLog = Join-Path $evidenceDir "bench-flash-$stamp.log"
$probeDir = Join-Path $evidenceDir "probe-$stamp"
New-Item -ItemType Directory -Path $probeDir -Force | Out-Null
Write-Host ""
Write-Host "Checking what is already on the board..." -ForegroundColor Cyan
$bootProbe = Join-Path $probeDir "at_0x0.bin"
$partProbe = Join-Path $probeDir "at_0x8000.bin"
& python -m esptool --chip esp32s3 --port $ComPort --baud $Baud `
    read_flash 0x0 0x4 $bootProbe *> $null
& python -m esptool --chip esp32s3 --port $ComPort --baud $Baud `
    read_flash 0x8000 0x4 $partProbe *> $null

$hasBootloader = $false
$hasPartTable  = $false
if (Test-Path $bootProbe) {
    $b = [System.IO.File]::ReadAllBytes($bootProbe)
    # 0xE9 is the ESP firmware-image magic byte.
    if ($b.Length -ge 1 -and $b[0] -eq 0xE9) { $hasBootloader = $true }
}
if (Test-Path $partProbe) {
    $p = [System.IO.File]::ReadAllBytes($partProbe)
    # 0xAA 0x50 is the partition-table entry magic.
    if ($p.Length -ge 2 -and $p[0] -eq 0xAA -and $p[1] -eq 0x50) { $hasPartTable = $true }
}
Write-Host ("  bootloader at 0x0    : {0}" -f $(if ($hasBootloader) { "present" } else { "ABSENT" }))
Write-Host ("  partition table 0x8000: {0}" -f $(if ($hasPartTable) { "present" } else { "ABSENT" }))

if (-not ($hasBootloader -and $hasPartTable)) {
    if (-not $Virgin) {
        Write-Host ""
        Write-Host "THIS BOARD IS NOT SET UP YET - nothing was written." -ForegroundColor Red
        Write-Host "" -ForegroundColor Red
        Write-Host "It has no bootloader and/or no partition table, so writing only the" -ForegroundColor Red
        Write-Host "application would leave a device that cannot boot and says nothing" -ForegroundColor Red
        Write-Host "about why. A first-time board needs the whole set written." -ForegroundColor Red
        Write-Host ""
        Write-Host "Re-run with -Virgin to do that:" -ForegroundColor Yellow
        Write-Host "    .\04_bench_flash.ps1 -Product $Product -Virgin" -ForegroundColor Yellow
        Write-Host ""
        Write-Host "-Virgin is for blank BENCH boards only. It writes at 0x0 and would" -ForegroundColor Yellow
        Write-Host "erase the identity of a commissioned unit." -ForegroundColor Yellow
        exit 1
    }
    Write-Host "  -> first-time setup: the full set will be written" -ForegroundColor Yellow
} elseif ($Virgin) {
    Write-Host ""
    Write-Host "This board ALREADY has a bootloader and partition table." -ForegroundColor Yellow
    Write-Host "-Virgin would overwrite them and reset the flash layout." -ForegroundColor Yellow
    $ok = Read-Host "Type VIRGIN to do it anyway, anything else to abort"
    if ($ok -ne "VIRGIN") { Write-Host "Aborted. Nothing was written." -ForegroundColor Cyan; exit 0 }
}

# ------------------------------------------------------------------ flash
Write-Host ""
if ($Virgin -or -not ($hasBootloader -and $hasPartTable)) {
    # Fetch the three support images for this product/version and write all
    # four at their own offsets. Deliberately NOT the FACTORY-ONLY merged
    # image: that one pads the gap between the partition table and boot_app0
    # with 0xFF, which blanks NVS. Writing per-offset leaves NVS alone, so this
    # same path stays safe if it is ever pointed at a provisioned unit.
    $verForAssets = $benchVersion
    $base = "https://github.com/consultifidata-cyber/covio/releases/download/v$verForAssets"
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $parts = @(
        @{ Offset = "0x0";    Name = "$($sel.Prefix)-v$verForAssets-bootloader.bin" },
        @{ Offset = "0x8000"; Name = "$($sel.Prefix)-v$verForAssets-partitions.bin" },
        @{ Offset = "0xe000"; Name = "$($sel.Prefix)-v$verForAssets-boot_app0.bin" }
    )
    foreach ($p in $parts) {
        $dest = Join-Path $benchDir $p.Name
        if (-not (Test-Path $dest)) {
            Write-Host "  fetching $($p.Name) ..."
            try {
                Invoke-WebRequest -Uri "$base/$($p.Name)" -OutFile $dest `
                    -Headers @{ "User-Agent" = "covio-plant-usb-kit" } -TimeoutSec 180
            } catch {
                Write-Host "Could not download $($p.Name): $($_.Exception.Message)" -ForegroundColor Red
                Write-Host "Nothing was written." -ForegroundColor Red
                exit 1
            }
        }
        $p.Path = $dest
    }
    Write-Host ""
    Write-Host "Writing the full set (bootloader, partitions, boot_app0, app) ..." -ForegroundColor Cyan
    & python -m esptool --chip esp32s3 --port $ComPort --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash --verify `
        0x0     $parts[0].Path `
        0x8000  $parts[1].Path `
        0xe000  $parts[2].Path `
        0x10000 $image 2>&1 | Tee-Object -FilePath $flashLog
} else {
    Write-Host "Writing the application at 0x10000 ..." -ForegroundColor Cyan
    & python -m esptool --chip esp32s3 --port $ComPort --baud $Baud `
        --before default_reset --after hard_reset `
        write_flash --verify 0x10000 $image 2>&1 | Tee-Object -FilePath $flashLog
}
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
# DTR MUST BE ASSERTED. On an ESP32-S3 built with ARDUINO_USB_CDC_ON_BOOT=1,
# Serial is TinyUSB CDC, and that layer only transmits while the host asserts
# DTR -- with DTR low the device writes into a void and the host reads 0 bytes.
# An earlier version of this kit held DTR low to avoid resetting the device and
# got exactly that: a completely silent console on a working board. Asserting
# DTR may reboot the unit on connect; that is survivable (queue lives in SPIFFS,
# totalizer is checkpointed) and the boot banner it produces is useful. RTS
# stays low -- it is the RTS/DTR *sequence* that drives the reset logic.
$port.DtrEnable = $true
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
