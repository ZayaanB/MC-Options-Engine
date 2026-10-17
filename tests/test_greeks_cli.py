import math
import os
from pathlib import Path
import re
import subprocess
import unittest


ENGINE = os.environ.get("MC_ENGINE", str(Path(__file__).resolve().parents[1] / "build/mcprice"))


def run(*arguments):
    return subprocess.run([ENGINE, *arguments], text=True, capture_output=True, timeout=30)


def values(output):
    labels = ("Delta", "Gamma", "Vega (per 1 vol point)")
    return tuple(float(re.search(rf"^{re.escape(label)}:\s+([^\s]+)", output,
                                 re.MULTILINE).group(1)) for label in labels)


class GreeksCliTests(unittest.TestCase):
    def test_call_and_put_match_analytical_greeks(self):
        d1 = 0.35
        density = math.exp(-0.5 * d1 * d1) / math.sqrt(2 * math.pi)
        call_delta = 0.5 * math.erfc(-d1 / math.sqrt(2))
        for kind, delta in (("call", call_delta), ("put", call_delta - 1)):
            with self.subTest(kind=kind):
                result = run("greeks", "--type", kind, "--paths", "200000", "--antithetic")
                self.assertEqual(result.returncode, 0, result.stderr)
                actual = values(result.stdout)
                for observed, expected, tolerance in zip(
                        actual, (delta, density / 20, density), (0.006, 0.002, 0.006)):
                    self.assertAlmostEqual(observed, expected, delta=tolerance)
                self.assertIn("Greek confidence intervals: not implemented", result.stdout)

    def test_combined_pricing_matches_standalone_and_repeats(self):
        inputs = ("--paths", "10000", "--threads", "2", "--seed", "7",
                  "--antithetic", "--batch-size", "1000",
                  "--spot-bump", "0.5", "--vol-bump", "0.005")
        standalone = run("greeks", *inputs)
        repeat = run("greeks", *inputs)
        combined = run("price", "--greeks", *inputs)
        for result in (standalone, repeat, combined):
            self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(values(standalone.stdout), values(repeat.stdout))
        self.assertEqual(values(standalone.stdout), values(combined.stdout))
        self.assertIn("Analytical price:", combined.stdout)
        self.assertIn("Monte Carlo estimate:", combined.stdout)
        self.assertIn("Spot bump:              0.500000", combined.stdout)
        self.assertIn("Volatility bump:        0.005000", combined.stdout)

    def test_expiry_and_zero_volatility(self):
        expiry = run("greeks", "--spot", "120", "--maturity", "0", "--paths", "1000")
        self.assertEqual(expiry.returncode, 0, expiry.stderr)
        self.assertEqual(values(expiry.stdout), (1.0, 0.0, 0.0))
        deterministic = run("greeks", "--volatility", "0", "--paths", "10000")
        self.assertEqual(deterministic.returncode, 0, deterministic.stderr)
        self.assertTrue(all(math.isfinite(value) for value in values(deterministic.stdout)))

    def test_invalid_inputs_fail_without_partial_results(self):
        cases = (
            ("greeks", "--type", "asian-call"),
            ("greeks", "--method", "analytical"),
            ("price", "--method", "analytical", "--greeks"),
            ("price", "--type", "asian-call", "--greeks"),
            ("price", "--spot-bump", "1"),
            ("greeks", "--spot-bump", "0"),
            ("greeks", "--spot-bump", "100"),
            ("greeks", "--spot-bump", "1e-300"),
            ("greeks", "--vol-bump", "-0.1"),
            ("greeks", "--vol-bump", "nan"),
            ("greeks", "--vol-bump"),
            ("greeks", "--paths", "3", "--antithetic"),
            ("greeks", "--threads", "0"),
            ("greeks", "--paths", "0"),
            ("greeks", "--greeks", "--greeks"),
        )
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = run(*arguments)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Error:", result.stderr)
                self.assertEqual(result.stdout, "")

    def test_price_defaults_stay_unchanged_and_help_lists_calculator(self):
        price = run("price", "--method", "analytical")
        self.assertEqual(price.returncode, 0, price.stderr)
        self.assertIn("Analytical price:", price.stdout)
        self.assertNotIn("Delta:", price.stdout)
        help_result = run("greeks", "--help")
        self.assertEqual(help_result.returncode, 0, help_result.stderr)
        self.assertIn("mcprice greeks", help_result.stdout)
        self.assertIn("--vol-bump", help_result.stdout)


if __name__ == "__main__":
    unittest.main()
