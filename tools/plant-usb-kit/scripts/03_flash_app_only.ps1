<#
    JOB 3 - REFLASH THE APPLICATION   (AUTHORIZED, OPTIONAL, NOT THE FRESHER'S CALL)

    Writes ONLY:
        0x10000  covio-firmware-v1.0.2.bin   (the application)
        0xe000   boot_app0.bin               (points the bootloader at app0)

    It does NOT touch:
        0x0      bootloader
        0x8000   partition table
        0x9000   NVS       <- API key, WiFi, server URL, totalizer, OTA floor
        0xC90000 SPIFFS    <- the queued readings

    0xe000 is 4KB-sector aligned and NVS ends at 0xDFFF, so the boot_app0 write
    cannot spill into NVS.

    This script will refuse to run without the founder's authorization phrase,
    refuses any merged image, and contains no erase path at all.

    THIS IS THE PLANT PATH, and it deliberately still flashes v1.0.2 -- the
    version closest to what the commissioned meter already runs. v1.1.0 exists
    and is published, but it has never run on hardware and it changes real
    runtime behaviour. Prove it on a bench unit first (04_bench_flash.ps1);
    only then consider bringing it to the plant.

    Usage:  .\03_flash_app_only.ps1 -Authorize 'FLASH-COVIO-APP-ONLY'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$Authorize,
    [string]$ComPort = "",
    [int]$Baud = 460800
)

$ErrorActionPreference = "Stop"
$kitRoot      = Split-Path -Parent $PSScriptRoot
$artifactDir  = Join-Path $kitRoot "artifacts\plant-v1.0.2"
$evidenceDir  = Join-Path $kitRoot "evidence"
$appImage     = Join-Path $artifactDir "covio-firmware-v1.0.2.bin"
$bootApp0     = Join-Path $artifactDir "boot_app0.bin"
$EXPECTED_APP = "B9556D7ABEE1BEE28DEEFE02C168349D77DB373A699AB5E027756EE31C46E8BF"
$EXPECTED_B0  = "F94C5D786A7A8FAB06AC5D10E33BF37711A6697636DC037559EA19CC410A17F0"

Write-Host ""
Write-Host "=== COVIO METER - JOB 3: REFLASH APPLICATION (app-only) ===" -ForegroundColor Cyan
Write-Host ""

# ------------------------------------------------------------------ gate 1: authorization
if ($Authorize -ne "FLASH-COVIO-APP-ONLY") {
    Write-Host "NOT AUTHORIZED. Nothing was written." -ForegroundColor Red
    Write-Host "This step reflashes live production equipment and needs the founder's" -ForegroundColor Red
    Write-Host "explicit go-ahead. If you were not given the phrase, you were not meant" -ForegroundColor Red
    Write-Host "to run this. Jobs 1 and 2 are the visit." -ForegroundColor Red
    exit 1
}

# ------------------------------------------------------------------ gate 2: evidence first
$captures = @(Get-ChildItem -Path $evidenceDir -Filter "serial-*.log" -ErrorAction SilentlyContinue)
if ($captures.Count -eq 0) {
    Write-Host "NO EVIDENCE CAPTURE FOUND. Nothing was written." -ForegroundColor Red
    Write-Host "" -ForegroundColor Red
    Write-Host "Run Job 1 first:  .\01_capture.ps1" -ForegroundColor Red
    Write-Host "Once you reflash, the current OTA rejection state is gone forever and" -ForegroundColor Red
    Write-Host "the reason the update was refused can never be recovered." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] Evidence capture present ($($captures.Count) file(s))." -ForegroundColor Green

# ------------------------------------------------------------------ gate 3: the images
foreach ($pair in @(@($appImage, $EXPECTED_APP, "application"), @($bootApp0, $EXPECTED_B0, "boot_app0"))) {
    $path = $pair[0]; $want = $pair[1]; $label = $pair[2]
    if (-not (Test-Path $path)) {
        Write-Host "MISSING: $path - nothing was written." -ForegroundColor Red
        exit 1
    }
    if ((Split-Path -Leaf $path) -match 'merged') {
        Write-Host "REFUSING: '$path' is a MERGED image." -ForegroundColor Red
        Write-Host "A merged image carries 0xFF across NVS and would erase the meter's" -ForegroundColor Red
        Write-Host "API key, WiFi, totalizer and OTA floor. Nothing was written." -ForegroundColor Red
        exit 1
    }
    $got = (Get-FileHash -Path $path -Algorithm SHA256).Hash
    if ($got -ne $want) {
        Write-Host "CHECKSUM MISMATCH on the $label image - nothing was written." -ForegroundColor Red
        Write-Host "  expected $want" -ForegroundColor Red
        Write-Host "  actual   $got" -ForegroundColor Red
        Write-Host "This image is not the one that was verified. Stop and call the founder." -ForegroundColor Red
        exit 1
    }
    Write-Host "[OK] $label image checksum verified." -ForegroundColor Green
}

# ------------------------------------------------------------------ gate 4: esptool
$esptoolOk = $false
try {
    $v = & python -m esptool version 2>&1
    if ($LASTEXITCODE -eq 0) { $esptoolOk = $true }
} catch { }
if (-not $esptoolOk) {
    Write-Host "esptool is not available. Run .\00_setup.ps1 first. Nothing was written." -ForegroundColor Red
    exit 1
}
Write-Host "[OK] esptool available." -ForegroundColor Green

# ---------------------------------------------------------------- find the port
if ([string]::IsNullOrWhiteSpace($ComPort)) {
    $candidates = @(Get-WmiObject Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like "*VID_303A*" -and $_.Name -match '\(COM\d+\)' })
    if ($candidates.Count -eq 1 -and $candidates[0].Name -match '\((COM\d+)\)') {
        $ComPort = $matches[1]
        Write-Host "[OK] Meter found on $ComPort" -ForegroundColor Green
    } else {
        Write-Host "Could not identify the port automatically. Re-run with -ComPort COMx" -ForegroundColor Red
        exit 1
    }
}

# ------------------------------------------------------------------ last confirmation
Write-Host ""
Write-Host "About to write to the LIVE meter on $ComPort :" -ForegroundColor Yellow
Write-Host "    0x10000  covio-firmware-v1.0.2.bin" -ForegroundColor Yellow
Write-Host "    0xe000   boot_app0.bin" -ForegroundColor Yellow
Write-Host "NVS (0x9000) and the reading queue (0xC90000) are not touched." -ForegroundColor Yellow
Write-Host ""
$confirm = Read-Host "Type FLASH to proceed, anything else to abort"
if ($confirm -ne "FLASH") {
    Write-Host "Aborted. Nothing was written." -ForegroundColor Cyan
    exit 0
}

$stamp   = Get-Date -Format "yyyyMMdd-HHmmss"
$flashLog = Join-Path $evidenceDir "flash-$stamp.log"

# ---- step 1: the application. If this fails we stop BEFORE touching otadata,
# ---- so the bootloader keeps pointing at whatever is currently bootable.
Write-Host ""
Write-Host "Step 1/2: writing the application at 0x10000 ..." -ForegroundColor Cyan
& python -m esptool --chip esp32s3 --port $ComPort --baud $Baud --before default_reset --after no_reset write_flash --verify 0x10000 $appImage 2>&1 | Tee-Object -FilePath $flashLog
if ($LASTEXITCODE -ne 0) {
    Write-Host ""
    Write-Host "APPLICATION WRITE FAILED (exit $LASTEXITCODE)." -ForegroundColor Red
    Write-Host "otadata was NOT touched, so the meter still boots its previous image." -ForegroundColor Yellow
    Write-Host "Power-cycle the meter and run Job 1 to confirm it came back. Then call the founder." -ForegroundColor Yellow
    exit 2
}
Write-Host "[OK] Application written and verified." -ForegroundColor Green

# ---- step 2: point the bootloader at app0.
Write-Host ""
Write-Host "Step 2/2: writing boot_app0 at 0xe000 ..." -ForegroundColor Cyan
& python -m esptool --chip esp32s3 --port $ComPort --baud $Baud --before default_reset --after hard_reset write_flash --verify 0xe000 $bootApp0 2>&1 | Tee-Object -FilePath $flashLog -Append
if ($LASTEXITCODE -ne 0) {
    Write-Host "boot_app0 write failed (exit $LASTEXITCODE). Call the founder before doing anything else." -ForegroundColor Red
    exit 2
}
Write-Host "[OK] boot_app0 written." -ForegroundColor Green

# ------------------------------------------------------------------ post-flash check
Write-Host ""
Write-Host "Waiting for the meter to boot, then reading it back..." -ForegroundColor Cyan
Start-Sleep -Seconds 6

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
    $after | Out-File -FilePath (Join-Path $evidenceDir "post-flash-show-$stamp.log") -Encoding utf8
    Write-Host ""
    Write-Host $after
    Write-Host ""
    if ($after -match 'fw\s*:\s*1\.0\.2') {
        Write-Host "CONFIRMED: the meter is now running 1.0.2." -ForegroundColor Green
    } else {
        Write-Host "Could not confirm 1.0.2 from the console output above." -ForegroundColor Yellow
        Write-Host "Run .\01_capture.ps1 and send the log to the founder." -ForegroundColor Yellow
    }
    if ($after -match 'api_key\s*:\s*default') {
        Write-Host ""
        Write-Host "WARNING: the meter reports the DEFAULT api_key. It will not meter." -ForegroundColor Red
        Write-Host "Call the founder immediately - the device needs re-provisioning." -ForegroundColor Red
    }
}
catch {
    Write-Host "Could not reopen the port to verify: $($_.Exception.Message)" -ForegroundColor Yellow
    Write-Host "Run .\01_capture.ps1 to check the meter came back." -ForegroundColor Yellow
}
finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}

Write-Host ""
Write-Host "Flash log: $flashLog"
