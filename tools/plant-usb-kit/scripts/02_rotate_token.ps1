<#
    JOB 2 - ROTATE THE DEVICE TOKEN

    Writes the new API key into the meter's NVS via the serial console's
    'set key' command. Nothing else is touched - WiFi, server URL, totalizer
    and the queued readings are all left exactly as they are.

    ORDER MATTERS. There is only one key slot on this device, so from the
    moment the ERP issues a new key until you type it in here, the meter's
    pushes are rejected. Readings are NOT lost (they queue in SPIFFS, roughly
    27 hours of headroom) but the gap must be closed the same day.

        1. Founder generates the new key in the ERP panel and reads it out.
        2. You run this script immediately and paste it in.
        3. You confirm the fingerprint changed.

    Usage:   .\02_rotate_token.ps1 -NewKey 'the-key-from-the-erp-panel'
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$NewKey,
    [string]$ComPort = ""
)

$ErrorActionPreference = "Stop"
$kitRoot     = Split-Path -Parent $PSScriptRoot
$evidenceDir = Join-Path $kitRoot "evidence"
if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir | Out-Null }
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"

Write-Host ""
Write-Host "=== COVIO METER - JOB 2: ROTATE TOKEN ===" -ForegroundColor Cyan
Write-Host ""

if ($NewKey.Trim().Length -lt 12) {
    Write-Host "That key looks too short ($($NewKey.Trim().Length) characters)." -ForegroundColor Red
    Write-Host "Check you pasted the whole thing from the ERP panel. Nothing was written." -ForegroundColor Red
    exit 1
}
if ($NewKey.Trim() -eq "dev-key-change-me") {
    Write-Host "That is the DEFAULT key. Writing it would stop the meter dead" -ForegroundColor Red
    Write-Host "(this firmware refuses to run with the default key). Nothing was written." -ForegroundColor Red
    exit 1
}

# ---------------------------------------------------------------- find the port
if ([string]::IsNullOrWhiteSpace($ComPort)) {
    $candidates = @(Get-WmiObject Win32_PnPEntity -ErrorAction SilentlyContinue |
        Where-Object { $_.PNPDeviceID -like "*VID_303A*" -and $_.Name -match '\(COM\d+\)' })
    if ($candidates.Count -eq 1 -and $candidates[0].Name -match '\((COM\d+)\)') {
        $ComPort = $matches[1]
        Write-Host "Meter found on $ComPort" -ForegroundColor Green
    } else {
        Write-Host "Could not identify the port automatically. Re-run with -ComPort COMx" -ForegroundColor Red
        exit 1
    }
}

function Get-Fingerprint([string]$consoleText) {
    foreach ($line in ($consoleText -split "`r?`n")) {
        if ($line -match 'api_key\s*:\s*(.+)$') { return $matches[1].Trim() }
    }
    return $null
}

$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
$port.DtrEnable   = $false
$port.RtsEnable   = $false
$port.ReadTimeout = 500
$port.NewLine     = "`n"

$transcript = New-Object System.Text.StringBuilder
try {
    $port.Open()
    Start-Sleep -Milliseconds 400

    # ---- BEFORE ----
    $port.WriteLine("show")
    Start-Sleep -Seconds 2
    $before = $port.ReadExisting()
    [void]$transcript.Append("--- BEFORE ---`n" + $before)
    $fpBefore = Get-Fingerprint $before

    Write-Host ""
    Write-Host "Before rotation: $fpBefore"

    # ---- WRITE ----
    Write-Host ""
    Write-Host "Writing the new key..." -ForegroundColor Yellow
    $port.WriteLine("set key " + $NewKey.Trim())
    Start-Sleep -Seconds 2
    $ack = $port.ReadExisting()
    [void]$transcript.Append("`n--- SET ---`n" + $ack)

    # ---- AFTER ----
    $port.WriteLine("show")
    Start-Sleep -Seconds 2
    $after = $port.ReadExisting()
    [void]$transcript.Append("`n--- AFTER ---`n" + $after)
    $fpAfter = Get-Fingerprint $after

    Write-Host "After rotation:  $fpAfter"
    Write-Host ""

    if ($null -eq $fpBefore -or $null -eq $fpAfter) {
        Write-Host "Could not read the fingerprint back from the console." -ForegroundColor Yellow
        Write-Host "The key MAY have been written. Do not retry blindly - report to the founder." -ForegroundColor Yellow
    }
    elseif ($fpBefore -eq $fpAfter) {
        Write-Host "THE FINGERPRINT DID NOT CHANGE - the rotation did not take effect." -ForegroundColor Red
        Write-Host "Report to the founder. Do not run anything else." -ForegroundColor Red
    }
    else {
        Write-Host "ROTATION CONFIRMED - the fingerprint changed." -ForegroundColor Green
        Write-Host ""
        Write-Host "Watch for the meter's next successful push (within a few minutes)." -ForegroundColor Cyan
        Write-Host "If pushes keep failing, the ERP-side key and this one do not match." -ForegroundColor Cyan
    }
}
finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}

# The transcript deliberately contains only the fingerprint, never the raw key -
# the firmware's 'show' command does not print the key itself.
$outPath = Join-Path $evidenceDir "token-rotation-$stamp.log"
$transcript.ToString() | Out-File -FilePath $outPath -Encoding utf8
Write-Host ""
Write-Host "Transcript saved to: $outPath"
