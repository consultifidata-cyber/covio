<#
    SETUP CHECK - safe to run any time, changes nothing on the meter.

    Jobs 1 and 2 (capture, token rotation) need nothing but Windows PowerShell.
    Only Job 3 (reflash) needs Python + esptool, and this script installs it.
#>
[CmdletBinding()]
param([switch]$InstallEsptool)

$kitRoot     = Split-Path -Parent $PSScriptRoot
$artifactDir = Join-Path $kitRoot "artifacts"

Write-Host ""
Write-Host "=== COVIO PLANT USB KIT - SETUP CHECK ===" -ForegroundColor Cyan
Write-Host ""

# ---- 1. the firmware images -------------------------------------------------
# PLANT images are hash-pinned here: they are the ones that may touch a machine
# in production, so the kit itself asserts what they must be.
$expected = @{
    "plant-v1.0.2\covio-firmware-v1.0.2.bin" = "B9556D7ABEE1BEE28DEEFE02C168349D77DB373A699AB5E027756EE31C46E8BF"
    "plant-v1.0.2\boot_app0.bin"             = "F94C5D786A7A8FAB06AC5D10E33BF37711A6697636DC037559EA19CC410A17F0"
}
Write-Host "PLANT images (v1.0.2 - the version closest to what the meter runs):"
foreach ($name in $expected.Keys) {
    $path = Join-Path $artifactDir $name
    if (-not (Test-Path $path)) {
        # Absent on a fresh clone BY DESIGN -- production firmware is not
        # committed and does not arrive as a side effect of cloning. Only a
        # concern if you are actually going to a plant.
        Write-Host "  [not fetched] $name" -ForegroundColor DarkGray
        Write-Host "                bench work does not need this;" -ForegroundColor DarkGray
        Write-Host "                for a plant visit run .\06_fetch_plant_firmware.ps1" -ForegroundColor DarkGray
        continue
    }
    $got = (Get-FileHash -Path $path -Algorithm SHA256).Hash
    if ($got -eq $expected[$name]) {
        Write-Host "  [OK]      $name" -ForegroundColor Green
    } else {
        Write-Host "  [CORRUPT] $name - do not use this kit, get a fresh copy" -ForegroundColor Red
    }
}

# ---- 1b. bench images ------------------------------------------------------
# BENCH images are verified against the .sha256 that GitHub Actions generated
# beside each binary at build time, rather than a hash pinned in this script --
# so a newer bench build can be dropped in without editing code.
$benchDir = Join-Path $artifactDir "bench"
$benchVersion = "not fetched yet"
$bvFile = Join-Path $benchDir "VERSION.txt"
if (Test-Path $bvFile) { $benchVersion = (Get-Content $bvFile -Raw).Trim() }
Write-Host ""
Write-Host "BENCH images ($benchVersion - CI-built, NEVER yet run on hardware):"
$benchBins = @(Get-ChildItem -Path $benchDir -Filter "*.bin" -ErrorAction SilentlyContinue)
if ($benchBins.Count -eq 0) {
    Write-Host "  [MISSING] no bench images present - run .\05_check_repo.ps1" -ForegroundColor Yellow
}
foreach ($b in $benchBins) {
    $sidecar = "$($b.FullName).sha256"
    if (-not (Test-Path $sidecar)) {
        Write-Host "  [NO CHECKSUM] $($b.Name) - will not be flashable" -ForegroundColor Red
        continue
    }
    $want = ((Get-Content $sidecar -Raw) -split '\s+')[0].Trim().ToUpper()
    $got  = (Get-FileHash -Path $b.FullName -Algorithm SHA256).Hash.ToUpper()
    if ($got -eq $want) {
        Write-Host "  [OK]      $($b.Name)" -ForegroundColor Green
    } else {
        Write-Host "  [CORRUPT] $($b.Name) - re-download from the release" -ForegroundColor Red
    }
}

# No whole-flash image may live in this kit, under any of its names. Both
# `merged` (the old naming) and `FACTORY-ONLY-full` (the release naming) blank
# NVS when written at 0x0. Recurse -- artifacts\ has per-version subfolders now.
Write-Host ""
$whole = @(Get-ChildItem -Path $artifactDir -Recurse -File -ErrorAction SilentlyContinue |
           Where-Object { $_.Name -like "*merged*.bin" -or $_.Name -like "*FACTORY-ONLY*.bin" })
if ($whole.Count -gt 0) {
    Write-Host "  DANGER: a whole-flash image is present in artifacts\ :" -ForegroundColor Red
    foreach ($m in $whole) { Write-Host "    $($m.Name)" -ForegroundColor Red }
    Write-Host "  Delete it. Written at 0x0 it erases the API key and totalizer," -ForegroundColor Red
    Write-Host "  and a release build with no API key will not boot." -ForegroundColor Red
} else {
    Write-Host "  [OK]      no whole-flash image present (correct - none belongs here)" -ForegroundColor Green
}

# ---- 2. can we see the meter? ----------------------------------------------
Write-Host ""
Write-Host "USB:"
$candidates = @(Get-WmiObject Win32_PnPEntity -ErrorAction SilentlyContinue |
    Where-Object { $_.PNPDeviceID -like "*VID_303A*" -and $_.Name -match '\(COM\d+\)' })
if ($candidates.Count -ge 1) {
    foreach ($c in $candidates) { Write-Host "  [OK]      $($c.Name)" -ForegroundColor Green }
} else {
    Write-Host "  [not found] No Espressif device on USB right now." -ForegroundColor Yellow
    $all = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($all.Count -eq 0) {
        Write-Host "              No serial ports at all - check the cable is a DATA cable." -ForegroundColor Yellow
    } else {
        Write-Host "              Ports present: $($all -join ', ')" -ForegroundColor Yellow
    }
    Write-Host "              This is fine if you have not plugged in yet." -ForegroundColor DarkGray
}

# ---- 3. python + esptool (Job 3 only) --------------------------------------
Write-Host ""
Write-Host "Python + esptool (only needed for Job 3, the reflash):"
$pyOk = $false
try {
    $pv = & python --version 2>&1
    if ($LASTEXITCODE -eq 0) { $pyOk = $true; Write-Host "  [OK]      $pv" -ForegroundColor Green }
} catch { }
if (-not $pyOk) {
    Write-Host "  [missing] Python is not installed." -ForegroundColor Yellow
    Write-Host "            Jobs 1 and 2 still work without it." -ForegroundColor DarkGray
    Write-Host "            For Job 3: install from https://www.python.org/downloads/" -ForegroundColor DarkGray
    Write-Host "            (tick 'Add python.exe to PATH' during install)" -ForegroundColor DarkGray
} else {
    $etOk = $false
    try {
        & python -m esptool version 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) { $etOk = $true }
    } catch { }
    if ($etOk) {
        Write-Host "  [OK]      esptool installed" -ForegroundColor Green
    } elseif ($InstallEsptool) {
        Write-Host "  installing esptool ..." -ForegroundColor Cyan
        & python -m pip install --quiet esptool
        if ($LASTEXITCODE -eq 0) {
            Write-Host "  [OK]      esptool installed" -ForegroundColor Green
        } else {
            Write-Host "  [failed]  could not install esptool (no internet?)" -ForegroundColor Red
        }
    } else {
        Write-Host "  [missing] esptool. Run:  .\00_setup.ps1 -InstallEsptool" -ForegroundColor Yellow
    }
}

Write-Host ""
# Point at the step that is actually next. On a fresh clone that is fetching
# firmware, not the capture -- sending a new engineer to the wrong command
# first is how a simple job turns into a support call.
if ($benchBins.Count -eq 0) {
    Write-Host "Next step: run  .\05_check_repo.ps1" -ForegroundColor Cyan
    Write-Host "           (fetches the firmware - none is stored in the repo)" -ForegroundColor Cyan
} else {
    Write-Host "Ready." -ForegroundColor Cyan
    Write-Host "  bench testing : .\04_bench_flash.ps1   then BENCH-TESTING.md" -ForegroundColor Cyan
    Write-Host "  plant visit   : .\01_capture.ps1       (read-only, always safe)" -ForegroundColor Cyan
}
Write-Host ""
