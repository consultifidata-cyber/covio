"""
Native-host tests for the Balaji V1 freeze remediation's calibration
validation tool (scripts/validate_meter_calibration.py).

Only the pure computation functions are tested here (compute_measured_k_factor
/ evaluate_calibration) -- no real device or network access is exercised,
matching this project's existing "pure function extracted for testability"
convention already used for ota_version_policy.h/credential_display.h and
this same test/native/ suite's other tool tests (test_sign_manifest_tool.py).

Run:
    python -m unittest discover -s test/native -v
"""

import os
import sys
import unittest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "..", "scripts"))
import validate_meter_calibration as vmc  # noqa: E402


class ComputeMeasuredKFactorTests(unittest.TestCase):
    def test_basic_computation(self):
        # 50,000 pulses over 50 real litres -> 1000 pulses/litre.
        self.assertEqual(vmc.compute_measured_k_factor(50000, 50.0), 1000.0)

    def test_zero_delta_pulses_is_zero_k_factor(self):
        self.assertEqual(vmc.compute_measured_k_factor(0, 10.0), 0.0)

    def test_rejects_zero_measured_volume(self):
        with self.assertRaises(ValueError):
            vmc.compute_measured_k_factor(1000, 0.0)

    def test_rejects_negative_measured_volume(self):
        with self.assertRaises(ValueError):
            vmc.compute_measured_k_factor(1000, -5.0)

    def test_rejects_negative_delta_pulses(self):
        # after < before -- a totalizer must never decrease; this indicates
        # a bad capture (wrong session file, device rebooted/reset between
        # captures), not a valid measurement.
        with self.assertRaises(ValueError):
            vmc.compute_measured_k_factor(-1, 10.0)


class EvaluateCalibrationTests(unittest.TestCase):
    def test_exact_match_passes_with_zero_deviation(self):
        result = vmc.evaluate_calibration(1000.0, 1000.0, tolerance_pct=2.0)
        self.assertTrue(result["passed"])
        self.assertAlmostEqual(result["deviation_pct"], 0.0)

    def test_within_tolerance_passes(self):
        # 1015 vs configured 1000 = +1.5% deviation, within +/-2%.
        result = vmc.evaluate_calibration(1015.0, 1000.0, tolerance_pct=2.0)
        self.assertTrue(result["passed"])
        self.assertAlmostEqual(result["deviation_pct"], 1.5)

    def test_just_outside_tolerance_fails(self):
        # +2.5% deviation, outside +/-2% tolerance.
        result = vmc.evaluate_calibration(1025.0, 1000.0, tolerance_pct=2.0)
        self.assertFalse(result["passed"])
        self.assertAlmostEqual(result["deviation_pct"], 2.5)

    def test_negative_deviation_is_evaluated_by_absolute_value(self):
        # measured LOWER than configured must fail just as readily as higher.
        result = vmc.evaluate_calibration(950.0, 1000.0, tolerance_pct=2.0)
        self.assertFalse(result["passed"])
        self.assertAlmostEqual(result["deviation_pct"], -5.0)

    def test_boundary_exactly_at_tolerance_passes(self):
        # Exactly +2.0% on a 2.0% tolerance -- inclusive boundary.
        result = vmc.evaluate_calibration(1020.0, 1000.0, tolerance_pct=2.0)
        self.assertTrue(result["passed"])

    def test_rejects_nonpositive_configured_k_factor(self):
        with self.assertRaises(ValueError):
            vmc.evaluate_calibration(1000.0, 0.0, tolerance_pct=2.0)
        with self.assertRaises(ValueError):
            vmc.evaluate_calibration(1000.0, -1.0, tolerance_pct=2.0)


class EndToEndScenarioTests(unittest.TestCase):
    """Reproduces the exact kind of session a technician would run, using
    only the pure functions (no device)."""

    def test_realistic_passing_session(self):
        before_pulses = 66015
        after_pulses = 116015  # 50,000 pulses dispensed
        measured_volume_liters = 50.0
        configured_k_factor = 1000.0  # matches commissioning docs' observed calib

        delta = after_pulses - before_pulses
        measured_k = vmc.compute_measured_k_factor(delta, measured_volume_liters)
        result = vmc.evaluate_calibration(measured_k, configured_k_factor, tolerance_pct=2.0)

        self.assertEqual(delta, 50000)
        self.assertEqual(measured_k, 1000.0)
        self.assertTrue(result["passed"])

    def test_realistic_failing_session_flags_miscalibration(self):
        # Real meter delivers 10% more pulses per litre than configured --
        # exactly the "typo'd K-factor" scenario flagged in the Product
        # Readiness Review as a silent-miscalibration risk.
        before_pulses = 0
        after_pulses = 55000
        measured_volume_liters = 50.0
        configured_k_factor = 1000.0

        delta = after_pulses - before_pulses
        measured_k = vmc.compute_measured_k_factor(delta, measured_volume_liters)
        result = vmc.evaluate_calibration(measured_k, configured_k_factor, tolerance_pct=2.0)

        self.assertEqual(measured_k, 1100.0)
        self.assertFalse(result["passed"])
        self.assertAlmostEqual(result["deviation_pct"], 10.0)


if __name__ == "__main__":
    unittest.main()
