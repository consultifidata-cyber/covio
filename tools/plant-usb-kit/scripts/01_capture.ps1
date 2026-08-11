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
    [string]$MeterHost = "covio-858428.local"
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

# ---------------------------------------------------------------- find the port
if ([string]::IsNullOrWhiteSpace($ComPort)) {
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
foreach ($ep in @("info", "status", "health", "metrics")) {
    $url = "http://$MeterHost/api/v1/$ep"
    try {
        $resp = Invoke-WebRequest -Uri $url -TimeoutSec 6 -UseBasicParsing -ErrorAction Stop
        $out  = Join-Path $evidenceDir "http-$ep-$stamp.json"
        $resp.Content | Out-File -FilePath $out -Encoding utf8
        Write-Host "  [OK]   $ep  -> $(Split-Path -Leaf $out)" -ForegroundColor Green
    } catch {
        Write-Host "  [skip] $ep  (not reachable from this laptop - this is fine)" -ForegroundColor DarkGray
    }
}

# --------------------------------------------------------------- serial capture
Write-Host ""
Write-Host "Opening $ComPort at 115200 and listening for $Minutes minute(s)." -ForegroundColor Cyan
Write-Host "Log file: $logPath"
Write-Host ""
Write-Host "Leave this window alone until it finishes. Do not unplug the cable." -ForegroundColor Yellow
Write-Host ""

$port = New-Object System.IO.Ports.SerialPort($ComPort, 115200, 'None', 8, 'One')
# Hold both control lines low: on an ESP32-S3's native USB-Serial/JTAG a
# DTR/RTS toggle is what triggers a reset. We do not want to reboot the meter.
$port.DtrEnable   = $false
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
