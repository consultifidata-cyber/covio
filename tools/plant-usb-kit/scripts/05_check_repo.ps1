<#
    CHECK THE REPO - fetch the latest published firmware into the BENCH folder

    Asks GitHub what the newest covio release is, compares it with what this
    kit already holds, and downloads the two product images if they differ.
    Every download is checked against the .sha256 that CI generated beside it.

    Touches no device. Safe to run any time, including with nothing plugged in.

    TWO RULES ARE BUILT IN, and neither is negotiable:

      1. It writes ONLY to artifacts\bench\. The plant folder is pinned by
         hand and is never updated by an automatic repo check -- the whole
         point of the bench/plant split is that a newly published build does
         not silently become the thing someone flashes at a plant.

      2. It refuses to download whole-flash images (FACTORY-ONLY / merged).
         Written at 0x0 they blank NVS, and no such file belongs in this kit.

    Usage:  .\05_check_repo.ps1
            .\05_check_repo.ps1 -WhatIf     # report only, download nothing
#>
[CmdletBinding()]
param(
    [switch]$WhatIf,
    [string]$Repo = "consultifidata-cyber/covio"
)

$ErrorActionPreference = "Stop"
$kitRoot  = Split-Path -Parent $PSScriptRoot
$benchDir = Join-Path $kitRoot "artifacts\bench"
if (-not (Test-Path $benchDir)) { New-Item -ItemType Directory -Path $benchDir | Out-Null }

# Windows PowerShell 5.1 still negotiates TLS 1.0 by default on some builds,
# which github.com refuses outright. Set it explicitly or every call below
# fails with an unhelpful "connection was closed" error.
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

Write-Host ""
Write-Host "=== CHECKING THE REPO FOR NEW FIRMWARE ===" -ForegroundColor Cyan
Write-Host "  repo: $Repo"

$localVersionFile = Join-Path $benchDir "VERSION.txt"
$localVersion = "none yet"
if (Test-Path $localVersionFile) { $localVersion = (Get-Content $localVersionFile -Raw).Trim() }
Write-Host "  this kit currently holds: $localVersion"
Write-Host ""

# ---------------------------------------------------------------- ask GitHub
try {
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" `
                             -Headers @{ "User-Agent" = "covio-plant-usb-kit" } `
                             -TimeoutSec 30
} catch {
    Write-Host "Could not reach GitHub: $($_.Exception.Message)" -ForegroundColor Red
    Write-Host ""
    Write-Host "This laptop may have no internet, or the plant network may block it." -ForegroundColor Yellow
    Write-Host "That is not a blocker: the kit already holds v$localVersion and can" -ForegroundColor Yellow
    Write-Host "flash it offline. Nothing was changed." -ForegroundColor Yellow
    exit 1
}

$latest = ($rel.tag_name -replace '^v', '')
Write-Host "  latest published release: $($rel.tag_name)  ($($rel.published_at))"
Write-Host ""

if ($latest -eq $localVersion) {
    Write-Host "Already up to date - the kit holds the latest release." -ForegroundColor Green
    Write-Host "Nothing to download." -ForegroundColor Green
    exit 0
}

Write-Host "Published: v$latest.  This kit holds: $localVersion." -ForegroundColor Yellow
Write-Host ""

# ------------------------------------------------------- work out what to get
$wanted = @()
foreach ($product in @("covio-oilflow", "miki-wire")) {
    $bin = "$product-v$latest-app.bin"
    $sha = "$bin.sha256"
    $binAsset = $rel.assets | Where-Object { $_.name -eq $bin }
    $shaAsset = $rel.assets | Where-Object { $_.name -eq $sha }
    if (-not $binAsset -or -not $shaAsset) {
        Write-Host "Release $($rel.tag_name) is missing $bin or its checksum." -ForegroundColor Red
        Write-Host "Refusing to half-update the kit. Nothing was changed." -ForegroundColor Red
        exit 1
    }
    $wanted += ,@($binAsset, $shaAsset)
}

foreach ($pair in $wanted) {
    foreach ($a in $pair) {
        if ($a.name -match 'FACTORY-ONLY|merged') {
            Write-Host "Refusing to download $($a.name) - whole-flash images do not belong in this kit." -ForegroundColor Red
            exit 1
        }
    }
    Write-Host ("  will fetch: {0}  ({1:N0} bytes)" -f $pair[0].name, $pair[0].size)
}

if ($WhatIf) {
    Write-Host ""
    Write-Host "-WhatIf: reported only, nothing downloaded." -ForegroundColor Cyan
    exit 0
}

# ------------------------------------------------------------------ download
# Into a staging folder first. A half-finished download must never be able to
# leave the bench folder in a state where some files are the new version and
# others are the old one.
$staging = Join-Path $benchDir ".staging"
if (Test-Path $staging) { Remove-Item $staging -Recurse -Force }
New-Item -ItemType Directory -Path $staging | Out-Null

Write-Host ""
foreach ($pair in $wanted) {
    foreach ($a in $pair) {
        $dest = Join-Path $staging $a.name
        Write-Host "  downloading $($a.name) ..."
        Invoke-WebRequest -Uri $a.browser_download_url -OutFile $dest `
                          -Headers @{ "User-Agent" = "covio-plant-usb-kit" } -TimeoutSec 300
    }
    $binPath = Join-Path $staging $pair[0].name
    $shaPath = Join-Path $staging $pair[1].name
    $want = ((Get-Content $shaPath -Raw) -split '\s+')[0].Trim().ToUpper()
    $got  = (Get-FileHash -Path $binPath -Algorithm SHA256).Hash.ToUpper()
    if ($got -ne $want) {
        Write-Host ""
        Write-Host "CHECKSUM MISMATCH on $($pair[0].name) - download is corrupt." -ForegroundColor Red
        Write-Host "  expected $want" -ForegroundColor Red
        Write-Host "  actual   $got" -ForegroundColor Red
        Write-Host "The kit was NOT changed. Try again, or use the version it already holds." -ForegroundColor Red
        Remove-Item $staging -Recurse -Force
        exit 1
    }
    Write-Host "  [OK] $($pair[0].name) checksum verified" -ForegroundColor Green
}

# ------------------------------------------- swap in, only once all is verified
Get-ChildItem -Path $benchDir -File | Where-Object { $_.Name -ne "VERSION.txt" } | Remove-Item -Force
Get-ChildItem -Path $staging -File | ForEach-Object { Move-Item $_.FullName (Join-Path $benchDir $_.Name) -Force }
Remove-Item $staging -Recurse -Force
Set-Content -Path $localVersionFile -Value $latest -Encoding ascii

Write-Host ""
Write-Host "BENCH FOLDER UPDATED: $localVersion -> v$latest" -ForegroundColor Green
Get-ChildItem -Path $benchDir -Filter "*.bin" | ForEach-Object { Write-Host "  $($_.Name)" }
Write-Host ""
Write-Host "The PLANT folder was not touched - it stays pinned by hand." -ForegroundColor Cyan
Write-Host ""
Write-Host "This firmware has NOT run on hardware. Flash it to a BENCH unit and" -ForegroundColor Yellow
Write-Host "work through BENCH-TESTING.md:  .\scripts\04_bench_flash.ps1" -ForegroundColor Yellow
