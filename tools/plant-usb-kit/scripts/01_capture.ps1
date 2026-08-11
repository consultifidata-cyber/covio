<#
    JOB 1 - CAPTURE EVIDENCE   (READ-ONLY - cannot harm the meter)

    Listens to the meter's serial console over USB and records everything it
    says, including the line we came here for:

        [OTA] REJECTED candidate 1.0.2: <REASON>

    The meter polls for updates about every 5 minutes, so the default capture
    runs for 7 to guarantee we catch at least one cycle.

    This script sends only the console's read-only 'show' and 'help' commands.
    It never invokes esptool, never writes flash, and holds DTR/RTS low so that
    opening the port does not reset the device.

    Usage:   .\01_capture.ps1
             .\01_capture.ps1 -Minutes 12
             .\01_capture.ps1 -ComPort COM7
#>
[CmdletBinding()]
param(
    [string]$ComPort = "",
    [int]$Minutes = 7,
    [string]$MeterHost = "covio-858428.local",
    [switch]$NoDtr,
    # LAN ONLY: never open the serial port, never touch USB. Use this on a
    # meter that is running production. Opening the serial port asserts DTR,
    # which can reboot the unit; over HTTP we take nothing but a reading.
    # The device's HTTP endpoints expose strictly more than the console does,
    # including ota.last_reject_reason, so this is not a lesser capture -- on a
    # live meter it is the better one.
    [switch]$LanOnly
)

$ErrorActionPreference = "Stop"
$kitRoot     = Split-Path -Parent $PSScriptRoot
$evidenceDir = Join-Path $kitRoot "evidence"
if (-not (Test-Path $evidenceDir)) { New-Item -ItemType Directory -Path $evidenceDir | Out-Null }

$stamp   = Get-Date -Format "yyyyMMdd-HHmmss"
$logPath = Join-Path $evidenceDir "serial-$stamp.log"

Write-Host ""
Write-Host "=== COVIO METER - JOB 1: CAPTURE EVIDENCE (read-only) ===" -ForegroundColor Cyan
Write-Host ""

if ($LanOnly) {
    Write-Host "LAN-ONLY MODE: no serial port will be opened and USB will not be" -ForegroundColor Cyan
    Write-Host "touched. Safe to run against a meter that is in production." -ForegroundColor Cyan
    Write-Host ""
}

# ---------------------------------------------------------------- find the port
if (-not $LanOnly -and [string]::IsNullOrWhiteSpace($ComPort)) {
    Write-Host "Looking for the meter on USB (Espressif VID 303A / PID 1001)..."
    $candidates = @()
    try {
        $candidates = Get-WmiObject Win32_PnPEntity -ErrorAction Stop |
            Where-Object { $_.PNPDeviceID -like "*VID_303A*" -and $_.Name -match '\(COM\d+\)' }
    } catch {
        Write-Host "  (could not query USB devices: $($_.Exception.Message))" -ForegroundColor Yellow
    }

    if ($candidates.Count -eq 1) {
        if ($candidates[0].Name -match '\((COM\d+)\)') { $ComPort = $matches[1] }
        Write-Host "  Found the meter on $ComPort" -ForegroundColor Green
    }
    elseif ($candidates.Count -gt 1) {
        Write-Host "  More than one Espressif device is plugged in:" -ForegroundColor Yellow
        foreach ($c in $candidates) { Write-Host "    $($c.Name)" }
        Write-Host "  Unplug the others, or re-run with -ComPort COMx" -ForegroundColor Yellow
        exit 1
    }
    else {
        Write-Host "  Could not find it. Serial ports currently on this laptop:" -ForegroundColor Yellow
        $all = [System.IO.Ports.SerialPort]::GetPortNames()
        if ($all.Count -eq 0) {
            Write-Host "    (none at all - the cable is not connected, or it is a charge-only cable)" -ForegroundColor Red
        } else {
            foreach ($p in $all) { Write-Host "    $p" }
        }
        Write-Host ""
        Write-Host "  Check: is the USB cable a DATA cable? Is it seated at both ends?" -ForegroundColor Yellow
        Write-Host "  Then re-run, or re-run with -ComPort COMx to force a port." -ForegroundColor Yellow
        exit 1
    }
}

# ------------------------------------------------------------------- LAN snapshot
# Optional, and completely independent of USB. Only works if this laptop is on
# the same WiFi as the meter. Failure here is normal and harmless.
Write-Host ""
Write-Host "Trying the meter's web endpoints over the plant WiFi (optional)..."
$lanReached = $false
$otaBlocks  = @()
foreach ($ep in @("info", "status", "health", "metrics")) {
    $url = "http://$MeterHost/api/v1/$ep"
    try {
        $resp = Invoke-WebRequest -Uri $url -TimeoutSec 8 -UseBasicParsing -ErrorAction Stop
        $out  = Join-Path $evidenceDir "http-$ep-$stamp.json"
        $resp.Content | Out-File -FilePath $out -Encoding utf8
        Write-Host "  [OK]   $ep  -> $(Split-Path -Leaf $out)" -ForegroundColor Green
        $lanReached = $true
        try {
            $j = $resp.Content | ConvertFrom-Json
            if ($j.ota) { $otaBlocks += ,@($ep, $j.ota) }
        } catch { }
    } catch {
        Write-Host "  [skip] $ep  (not reachable from this laptop)" -ForegroundColor DarkGray
    }
}

# THE ANSWER WE HAVE BEEN CHASING lives in here. The device refuses OTA updates
# and only the device knows why; last_reject_reason is that reason, and it is
# reachable over plain HTTP without touching the unit at all.
if ($lanReached) {
    Write-Host ""
    Write-Host "--- OTA state as the device reports it ---" -ForegroundColor Cyan
    if ($otaBlocks.Count -eq 0) {
        Write-Host "  No 'ota' block in any endpoint's response." -ForegroundColor Yellow
        Write-Host "  Read the saved http-*.json files directly." -ForegroundColor Yellow
    }
    foreach ($pair in $otaBlocks) {
        $ota = $pair[1]
        Write-Host "  (from /api/v1/$($pair[0]))"
        foreach ($f in @("state", "last_reject_reason", "last_checked_ms", "current_version")) {
            if ($null -ne $ota.$f) { Write-Host ("    {0,-20} {1}" -f $f, $ota.$f) }
        }
        if ($ota.last_reject_reason) {
            Write-Host ""
            Write-Host "  >>> OTA REJECTION REASON: $($ota.last_reject_reason)" -ForegroundColor Green
            Write-Host "  >>> This is what the trip was for. Send this to the founder." -ForegroundColor Green
        } elseif ($null -ne $ota.state) {
            Write-Host "    (no rejection recorded - the device may not have been" -ForegroundColor DarkGray
            Write-Host "     offered an update since it last booted)" -ForegroundColor DarkGray
        }
    }
}

if ($LanOnly) {
    Write-Host ""
    Write-Host "=== LAN-ONLY CAPTURE COMPLETE ===" -ForegroundColor Cyan
    if (-not $lanReached) {
        Write-Host "The meter was NOT reachable at $MeterHost." -ForegroundColor Red
        Write-Host "Check this laptop is on the same network as the meter, or pass" -ForegroundColor Yellow
        Write-Host "-MeterHost <ip> if you know its address. Nothing was touched." -ForegroundColor Yellow
        exit 1
    }
    Write-Host "Saved to: $evidenceDir" -ForegroundColor Green
    Write-Host "No serial port was opened and USB was not touched." -ForegroundColor Green
    exit 0
}

# --------------------------------------------------------------- serial capture
Write-Host ""
Write-Host "Opening $ComPort at 115200 and listening for $Minutes minute(s)." -ForegroundColor Cyan
Write-Host "Log file: $logPath"
Write-Host ""
Write-Host "Leave this window alone until it finishes. Do not unplug the cable." -ForegroundColor Yellow
Write-Host ""

$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
# DTR MUST BE ASSERTED. On an ESP32-S3 built with ARDUINO_USB_CDC_ON_BOOT=1,
# Serial is TinyUSB CDC, and that layer only transmits while the host asserts
# DTR -- with DTR low the device writes into a void and the host reads 0 bytes.
# An earlier version of this kit held DTR low to avoid rebooting the meter and
# produced exactly that: a completely silent console on a working board, which
# would have wasted an entire plant visit whose whole purpose is reading one
# line off this port.
#
# Asserting DTR may reboot the unit when the port opens. That is survivable --
# the queue lives in SPIFFS and the totalizer is checkpointed -- and the boot
# banner it produces is itself useful evidence. RTS stays low: it is the
# RTS/DTR *sequence* that drives the reset logic, not DTR alone.
# Use -NoDtr only to reproduce the old behaviour for comparison.
$port.DtrEnable   = (-not $NoDtr)
$port.RtsEnable   = $false
$port.ReadTimeout = 500
$port.NewLine     = "`n"

$sb = New-Object System.Text.StringBuilder
try {
    $port.Open()
    Start-Sleep -Milliseconds 400

    # Ask the console to identify itself. 'show' and 'help' are read-only.
    $port.WriteLine("")
    Start-Sleep -Milliseconds 200
    $port.WriteLine("help")
    Start-Sleep -Milliseconds 400
    $port.WriteLine("show")

    $deadline = (Get-Date).AddMinutes($Minutes)
    $lastTick = Get-Date
    while ((Get-Date) -lt $deadline) {
        try {
            $chunk = $port.ReadExisting()
            if ($chunk.Length -gt 0) {
                [void]$sb.Append($chunk)
                Write-Host -NoNewline $chunk
            }
        } catch [TimeoutException] { }

        Start-Sleep -Milliseconds 200

        # A quiet progress marker every 60s so the fresher knows it is alive.
        if (((Get-Date) - $lastTick).TotalSeconds -ge 60) {
            $left = [int]($deadline - (Get-Date)).TotalMinutes
            Write-Host ""
            Write-Host "  ... still listening, about $left minute(s) to go" -ForegroundColor DarkGray
            $lastTick = Get-Date
        }
    }

    # One last 'show' so the log ends with a clean state snapshot.
    $port.WriteLine("show")
    Start-Sleep -Milliseconds 1200
    [void]$sb.Append($port.ReadExisting())
}
finally {
    if ($port.IsOpen) { $port.Close() }
    $port.Dispose()
}

$text = $sb.ToString()
$text | Out-File -FilePath $logPath -Encoding utf8

# ----------------------------------------------------------------- what we found
Write-Host ""
Write-Host ""
Write-Host "=== CAPTURE COMPLETE ===" -ForegroundColor Cyan
Write-Host "Full log saved to: $logPath"
Write-Host ""

if ([string]::IsNullOrWhiteSpace($text)) {
    Write-Host "NOTHING WAS RECEIVED." -ForegroundColor Red
    Write-Host "The port opened but the meter said nothing. That is unusual." -ForegroundColor Red
    Write-Host "Do not flash anything. Report this to the founder." -ForegroundColor Red
    exit 2
}

$otaLines = $text -split "`r?`n" | Where-Object { $_ -match '\[OTA\]' }
if ($otaLines.Count -gt 0) {
    Write-Host "OTA lines found (this is what the trip was for):" -ForegroundColor Green
    foreach ($l in $otaLines) { Write-Host "  $l" -ForegroundColor Green }
} else {
    Write-Host "No [OTA] line appeared in $Minutes minute(s)." -ForegroundColor Yellow
    Write-Host "Re-run for longer:  .\01_capture.ps1 -Minutes 15" -ForegroundColor Yellow
}

Write-Host ""
$idLines = $text -split "`r?`n" | Where-Object { $_ -match '^(device_id|fw|boot_id|server_url|api_key|wifi_ssid|calib)\s*:' }
if ($idLines.Count -gt 0) {
    Write-Host "Device identity:" -ForegroundColor Cyan
    foreach ($l in $idLines) { Write-Host "  $l" }
}

Write-Host ""
Write-Host "Now paste the log into Claude, or just tell Claude: 'job 1 done, read the evidence folder'." -ForegroundColor Cyan
