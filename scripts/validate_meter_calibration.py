#!/usr/bin/env python3
"""
validate_meter_calibration.py -- Balaji V1 freeze remediation (Product
Readiness Review P0-8 / freeze-list item 2: "real sensor/flow-accuracy
validation").

Computes a device's ACTUAL pulses-per-litre constant from a real,
technician-measured flow test, and compares it against the K-factor
currently configured for that device -- so "is the calibration correct"
becomes a repeatable, documented measurement instead of an untested
assumption.

This tool does NOT change the calibration architecture (K-factor stays
exactly where it already lives -- server-side, per store.h's own comment:
"K-factor itself is NOT stored here; it lives server-side"). It is a
READ-ONLY validation aid: it reads the device's local, unauthenticated
/api/v1/status endpoint (totalizer_raw_pulses) and does not write anything
to the device or the server. The technician supplies the currently
configured K-factor by hand (read via the serial console's `show` command,
or the server's calibration record) -- this tool has no way to read
calibration itself, matching the existing local-API contract exactly.

WHAT YOU NEED TO RUN A REAL VALIDATION:
  - A calibrated reference volume (a container of known, accurately
    measured capacity, or a certified flow reference) to dispense a known
    real quantity of oil through the meter during the test window.
  - Network access to the device's local API (same Wi-Fi, or the device's
    IP/mDNS name).
  - The K-factor currently configured on the device (serial `show` command).

Typical session:
    # 1) Immediately before starting the timed real-flow test:
    python scripts/validate_meter_calibration.py capture-before \\
        --device-host covio-858428.local --session /tmp/calib_session.json

    # 2) Dispense a known volume through the meter using your calibrated
    #    reference (a real physical action this tool cannot perform).

    # 3) Immediately after the test:
    python scripts/validate_meter_calibration.py capture-after \\
        --device-host covio-858428.local --session /tmp/calib_session.json \\
        --measured-volume-liters 50.0 --configured-k-factor 1000.0 \\
        --technician "Name" --out /tmp/calib_report.json

The report states measured K-factor, percent deviation from the configured
value, and a pass/fail verdict against --tolerance-pct (default 2.0%).

Also supports a single-shot, fully-scripted invocation (no live device, no
session file) for automation/testing:
    python scripts/validate_meter_calibration.py evaluate \\
        --before-pulses 1000000 --after-pulses 1050000 \\
        --measured-volume-liters 50.0 --configured-k-factor 1000.0
"""
import argparse
import json
import os
import sys
import urllib.error
import urllib.request

# Uses only the Python standard library -- no new project dependency, per
# this codebase's own "no new dependency without an ADR" governance rule
# (MASTER_GOVERNANCE.md), same reasoning sync.h/telemetry.h already apply
# on the firmware side to their own JSON handling.


def compute_measured_k_factor(delta_pulses, measured_volume_liters):
    """Pulses per litre actually observed during the test window."""
    if measured_volume_liters <= 0:
        raise ValueError("measured_volume_liters must be > 0")
    if delta_pulses < 0:
        raise ValueError("delta_pulses must be >= 0 (after must be >= before)")
    return delta_pulses / measured_volume_liters


def evaluate_calibration(measured_k_factor, configured_k_factor, tolerance_pct):
    """Compare the measured K-factor against what the device is currently
    configured with. Returns a dict -- never raises for an out-of-tolerance
    result (that is a normal, expected outcome of a validation, not an
    error); only raises for a nonsensical input (configured_k_factor <= 0)."""
    if configured_k_factor <= 0:
        raise ValueError("configured_k_factor must be > 0")
    deviation_pct = (measured_k_factor - configured_k_factor) / configured_k_factor * 100.0
    passed = abs(deviation_pct) <= tolerance_pct
    return {
        "measured_k_factor": measured_k_factor,
        "configured_k_factor": configured_k_factor,
        "deviation_pct": deviation_pct,
        "tolerance_pct": tolerance_pct,
        "passed": passed,
    }


def fetch_totalizer_raw_pulses(device_host, port=80, timeout_s=8):
    """Reads the device's own read-only /api/v1/status endpoint (the SAME
    endpoint used throughout this project's commissioning sessions) and
    returns totalizer_raw_pulses. No write of any kind is performed against
    the device by this tool."""
    url = f"http://{device_host}:{port}/api/v1/status"
    with urllib.request.urlopen(url, timeout=timeout_s) as resp:
        body = json.loads(resp.read().decode("utf-8"))
    if "totalizer_raw_pulses" not in body:
        raise RuntimeError(f"device response at {url} has no totalizer_raw_pulses field")
    return int(body["totalizer_raw_pulses"])


def _now_iso():
    # Deliberately uses the standard library's real wall-clock time (this is
    # operator-run tooling on a laptop with a real clock, unlike the device
    # itself, which has no RTC -- see sync.h's own documented limitation).
    import datetime
    return datetime.datetime.now().isoformat()


def cmd_capture_before(args):
    pulses = fetch_totalizer_raw_pulses(args.device_host, args.port, args.timeout_s)
    session = {"before_pulses": pulses, "before_captured_at": _now_iso(),
               "device_host": args.device_host}
    with open(args.session, "w") as f:
        json.dump(session, f, indent=2)
    print(f"Captured BEFORE totalizer_raw_pulses={pulses} at {session['before_captured_at']}")
    print(f"Session saved to {args.session}. Now dispense the known reference volume, "
          f"then run 'capture-after' with the same --session file.")


def cmd_capture_after(args):
    with open(args.session) as f:
        session = json.load(f)
    if "before_pulses" not in session:
        print(f"ERROR: {args.session} has no before_pulses -- run 'capture-before' first.",
              file=sys.stderr)
        sys.exit(1)

    after_pulses = fetch_totalizer_raw_pulses(args.device_host, args.port, args.timeout_s)
    delta = after_pulses - session["before_pulses"]

    measured_k = compute_measured_k_factor(delta, args.measured_volume_liters)
    result = evaluate_calibration(measured_k, args.configured_k_factor, args.tolerance_pct)

    report = {
        "device_host": args.device_host,
        "technician": args.technician,
        "before_captured_at": session["before_captured_at"],
        "after_captured_at": _now_iso(),
        "before_pulses": session["before_pulses"],
        "after_pulses": after_pulses,
        "delta_pulses": delta,
        "measured_volume_liters": args.measured_volume_liters,
        **result,
    }
    _print_and_write(report, args.out)


def cmd_evaluate(args):
    delta = args.after_pulses - args.before_pulses
    measured_k = compute_measured_k_factor(delta, args.measured_volume_liters)
    result = evaluate_calibration(measured_k, args.configured_k_factor, args.tolerance_pct)
    report = {
        "technician": args.technician,
        "before_pulses": args.before_pulses,
        "after_pulses": args.after_pulses,
        "delta_pulses": delta,
        "measured_volume_liters": args.measured_volume_liters,
        **result,
    }
    _print_and_write(report, args.out)


def _print_and_write(report, out_path):
    verdict = "PASS" if report["passed"] else "FAIL"
    print(f"delta_pulses          = {report['delta_pulses']}")
    print(f"measured_volume_liters= {report['measured_volume_liters']}")
    print(f"measured_k_factor     = {report['measured_k_factor']:.4f}")
    print(f"configured_k_factor   = {report['configured_k_factor']:.4f}")
    print(f"deviation_pct         = {report['deviation_pct']:+.3f}%  "
          f"(tolerance +/-{report['tolerance_pct']:.2f}%)")
    print(f"VERDICT: {verdict}")
    if out_path:
        with open(out_path, "w") as f:
            json.dump(report, f, indent=2)
        print(f"Report written to {out_path}")
    if not report["passed"]:
        sys.exit(1)


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = p.add_subparsers(dest="cmd", required=True)

    common_out = dict(default=None, help="Write the JSON validation report to this path")
    common_tol = dict(type=float, default=2.0, help="Acceptable deviation from configured K-factor, in percent (default 2.0)")

    p_before = sub.add_parser("capture-before", help="Capture the totalizer reading BEFORE a timed real-flow test")
    p_before.add_argument("--device-host", required=True)
    p_before.add_argument("--port", type=int, default=80)
    p_before.add_argument("--timeout-s", type=float, default=8)
    p_before.add_argument("--session", required=True, help="Path to save session state to")
    p_before.set_defaults(func=cmd_capture_before)

    p_after = sub.add_parser("capture-after", help="Capture the totalizer reading AFTER the test and evaluate calibration")
    p_after.add_argument("--device-host", required=True)
    p_after.add_argument("--port", type=int, default=80)
    p_after.add_argument("--timeout-s", type=float, default=8)
    p_after.add_argument("--session", required=True, help="Path to the session file written by capture-before")
    p_after.add_argument("--measured-volume-liters", type=float, required=True,
                          help="Real volume dispensed during the test, measured with a calibrated reference")
    p_after.add_argument("--configured-k-factor", type=float, required=True,
                          help="K-factor currently configured on the device (read via serial 'show')")
    p_after.add_argument("--tolerance-pct", **common_tol)
    p_after.add_argument("--technician", default=None)
    p_after.add_argument("--out", **common_out)
    p_after.set_defaults(func=cmd_capture_after)

    p_eval = sub.add_parser("evaluate", help="Fully-scripted evaluation from already-known pulse counts (no live device needed)")
    p_eval.add_argument("--before-pulses", type=int, required=True)
    p_eval.add_argument("--after-pulses", type=int, required=True)
    p_eval.add_argument("--measured-volume-liters", type=float, required=True)
    p_eval.add_argument("--configured-k-factor", type=float, required=True)
    p_eval.add_argument("--tolerance-pct", **common_tol)
    p_eval.add_argument("--technician", default=None)
    p_eval.add_argument("--out", **common_out)
    p_eval.set_defaults(func=cmd_evaluate)

    args = p.parse_args()
    try:
        args.func(args)
    except (urllib.error.URLError, OSError) as e:
        print(f"ERROR: could not reach device: {e}", file=sys.stderr)
        sys.exit(2)


if __name__ == "__main__":
    main()
