'use strict';

// wifiScan.js -- best-effort nearby-SoftAP detection for the provisioning
// wizard (§8: "AP-mode-aware setup wizard (detects Covio-Setup-* SSID...").
//
// Node has no built-in, cross-platform WiFi-scanning API, and adding a
// native module for this would be disproportionate to what's needed here.
// Windows ships `netsh wlan show networks`, a standard, always-available
// command-line tool -- shelled out to via execFile (never a shell string,
// so there is no command-injection surface: no user input reaches this
// call at all, it takes fixed arguments only).
//
// This is READ-ONLY and INFORMATIONAL ONLY: it never attempts to actually
// join a network (that would need elevated privileges and risks disrupting
// the operator's own saved WiFi profiles) -- the operator always connects
// manually via Windows' own WiFi settings; this just tells them which
// "Covio-Setup-*" networks are visible right now so they know which one to
// pick. Best-effort: any failure (non-Windows, adapter disabled, netsh
// missing, unexpected/localized output) degrades to an empty result, never
// a crash or a thrown error the wizard has to handle specially.

const { execFile } = require('child_process');

const SSID_PREFIX = 'Covio-Setup-';
const TIMEOUT_MS = 5000;

function scanNearbySetupNetworks() {
  return new Promise((resolve) => {
    if (process.platform !== 'win32') {
      resolve({ supported: false, ssids: [] });
      return;
    }
    execFile(
      'netsh',
      ['wlan', 'show', 'networks'],
      { timeout: TIMEOUT_MS, windowsHide: true },
      (err, stdout) => {
        if (err || !stdout) {
          resolve({ supported: true, ssids: [], note: 'WiFi scan unavailable (adapter off, or no permission).' });
          return;
        }
        resolve({ supported: true, ssids: parseSsids(stdout) });
      }
    );
  });
}

// `netsh wlan show networks` output lines look like:
//   "SSID 1 : Covio-Setup-A1B2"
// This label format has been stable across observed Windows locales; if a
// given system's output doesn't match, this simply yields an empty match
// list (see the best-effort note above) rather than a parse error.
function parseSsids(stdout) {
  const found = new Set();
  const lines = stdout.split(/\r?\n/);
  for (const line of lines) {
    const m = line.match(/SSID\s+\d+\s*:\s*(.+)$/);
    if (!m) continue;
    const ssid = m[1].trim();
    if (ssid.startsWith(SSID_PREFIX)) found.add(ssid);
  }
  return Array.from(found);
}

module.exports = { scanNearbySetupNetworks, parseSsids, SSID_PREFIX };
