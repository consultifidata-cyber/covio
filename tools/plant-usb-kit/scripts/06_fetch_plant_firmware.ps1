<#
    FETCH PLANT FIRMWARE - deliberately, and only when it is actually needed

    Downloads v1.0.2 (the version closest to what the commissioned Balaji meter
    runs) into artifacts\plant-v1.0.2\ and verifies it against a hash pinned in
    this script.

    This is a SEPARATE, EXPLICIT step on purpose. Production firmware does not
    arrive on a laptop as a side effect of cloning the repo or of a routine
    bench update -- somebody has to ask for it. A bench engineer never needs to
    run this, and should not.

    Touches no device.

    Usage:  .\06_fetch_plant_firmware.ps1
#>
[CmdletBinding()]
param([string]$Repo = "consultifidata-cyber/covio")

$ErrorActionPreference = "Stop"
$kitRoot   = Split-Path -Parent $PSScriptRoot
$plantDir  = Join-Path $kitRoot "artifacts\plant-v1.0.2"
$binName   = "covio-firmware-v1.0.2.bin"
# Pinned, not fetched from a sidecar: this is the image that may touch a machine
# in production, so the kit itself asserts what it must be. Verified against the
# published v1.0.2 release asset.
$EXPECTED  = "B9556D7ABEE1BEE28DEEFE02C168349D77DB373A699AB5E027756EE31C46E8BF"

if (-not (Test-Path $plantDir)) { New-Item -ItemType Directory -Path $plantDir | Out-Null }
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host ""
Write-Host "=== FETCH PLANT FIRMWARE (v1.0.2) ===" -ForegroundColor Cyan
Write-Host ""
Write-Host "This downloads the PRODUCTION firmware for the Balaji oil flow meter." -ForegroundColor Yellow
Write-Host "If you are doing bench testing, you do not need this. Ctrl+C now." -ForegroundColor Yellow
Write-Host ""

$dest = Join-Path $plantDir $binName
if (Test-Path $dest) {
    $got = (Get-FileHash -Path $dest -Algorithm SHA256).Hash.ToUpper()
    if ($got -eq $EXPECTED) {
        Write-Host "Already present and verified: $binName" -ForegroundColor Green
        exit 0
    }
    Write-Host "A file is present but its hash does not match. Replacing it." -ForegroundColor Yellow
}

$url = "https://github.com/$Repo/releases/download/v1.0.2/$binName"
Write-Host "Downloading $binName ..."
$tmp = "$dest.partial"
try {
    Invoke-WebRequest -Uri $url -OutFile $tmp `
                      -Headers @{ "User-Agent" = "covio-plant-usb-kit" } -TimeoutSec 300
} catch {
    Write-Host "Download failed: $($_.Exception.Message)" -ForegroundColor Red
    if (Test-Path $tmp) { Remove-Item $tmp -Force }
    exit 1
}

$got = (Get-FileHash -Path $tmp -Algorithm SHA256).Hash.ToUpper()
if ($got -ne $EXPECTED) {
    Write-Host ""
    Write-Host "CHECKSUM MISMATCH - the download is not the expected image." -ForegroundColor Red
    Write-Host "  expected $EXPECTED" -ForegroundColor Red
    Write-Host "  actual   $got" -ForegroundColor Red
    Write-Host "Discarded. Nothing usable was written." -ForegroundColor Red
    Remove-Item $tmp -Force
    exit 1
}
Move-Item $tmp $dest -Force
"$($got.ToLower())  $binName" | Out-File -FilePath "$dest.sha256" -Encoding ascii
Write-Host "[OK] $binName downloaded and verified" -ForegroundColor Green

# boot_app0 is the framework-supplied OTA-data init image: 8 KB, and identical
# across releases because it comes from the pinned platform, not from this
# project's sources. Verified byte-identical between the v1.0.0 build set and
# the v1.1.0 release asset, so fetching the published one is safe and keeps
# this repo free of committed binaries.
$B0_EXPECTED = "F94C5D786A7A8FAB06AC5D10E33BF37711A6697636DC037559EA19CC410A17F0"
$b0Dest = Join-Path $plantDir "boot_app0.bin"
$needB0 = $true
if (Test-Path $b0Dest) {
    if ((Get-FileHash -Path $b0Dest -Algorithm SHA256).Hash.ToUpper() -eq $B0_EXPECTED) {
        Write-Host "[OK] boot_app0.bin already present and verified" -ForegroundColor Green
        $needB0 = $false
    }
}
if ($needB0) {
    $b0Url = "https://github.com/$Repo/releases/download/v1.1.0/covio-oilflow-v1.1.0-boot_app0.bin"
    Write-Host "Downloading boot_app0.bin ..."
    $b0Tmp = "$b0Dest.partial"
    try {
        Invoke-WebRequest -Uri $b0Url -OutFile $b0Tmp `
                          -Headers @{ "User-Agent" = "covio-plant-usb-kit" } -TimeoutSec 120
    } catch {
        Write-Host "boot_app0 download failed: $($_.Exception.Message)" -ForegroundColor Red
        if (Test-Path $b0Tmp) { Remove-Item $b0Tmp -Force }
        exit 1
    }
    $b0Got = (Get-FileHash -Path $b0Tmp -Algorithm SHA256).Hash.ToUpper()
    if ($b0Got -ne $B0_EXPECTED) {
        Write-Host "CHECKSUM MISMATCH on boot_app0.bin - discarded." -ForegroundColor Red
        Write-Host "  expected $B0_EXPECTED" -ForegroundColor Red
        Write-Host "  actual   $b0Got" -ForegroundColor Red
        Remove-Item $b0Tmp -Force
        exit 1
    }
    Move-Item $b0Tmp $b0Dest -Force
    Write-Host "[OK] boot_app0.bin downloaded and verified" -ForegroundColor Green
}

Write-Host ""
Write-Host "Plant firmware ready. Run .\00_setup.ps1 to confirm, then follow" -ForegroundColor Cyan
Write-Host "START-HERE.md. Reflashing a production meter still needs the" -ForegroundColor Cyan
Write-Host "founder's authorization phrase." -ForegroundColor Cyan
