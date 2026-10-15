import csv
import json
import math
import subprocess
import sys
import tempfile
import unittest
from datetime import date, timedelta
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
import residual_forecasts as residual


class ResidualTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        self.dates = []
        day = date(2020, 1, 1)
        while len(self.dates) < 700:
            if day.weekday() < 5:
                self.dates.append(day.isoformat())
            day += timedelta(days=1)
        self.config = {"schema_version": 1, "lookback_days": 60,
                       "validation_start": self.dates[500], "holdout_start": self.dates[660],
                       "test_end": self.dates[-1], "assets": []}
        for symbol, prices in (("FLAT", [100.0] * 700),
                               ("WAVE", [100 * math.exp(0.0001 * i + 0.003 * math.sin(i)) for i in range(700)])):
            history = self.directory / f"{symbol}.csv"
            with history.open("w", newline="") as stream:
                writer = csv.writer(stream)
                writer.writerow(["Date", "Adj Close"])
                writer.writerows(zip(self.dates, prices))
            metadata = self.directory / f"{symbol}.meta"
            metadata.write_text(f"provider=synthetic\nsymbol={symbol}\nadjustment=none\n"
                                f"retrieved_on={self.dates[-1]}\nsource_url=local:fixture\n"
                                "price_column=Adj Close\nfrequency=daily\n", encoding="utf-8")
            self.config["assets"].append({"symbol": symbol, "csv": history.name,
                "metadata": metadata.name, "csv_sha256": residual.sha256(history.read_bytes()),
                "metadata_sha256": residual.sha256(metadata.read_bytes())})
        self.manifest = self.directory / "manifest.json"
        self.save()

    def save(self):
        self.manifest.write_text(json.dumps(self.config), encoding="utf-8")

    def test_empirical_quantiles_probabilities_and_limits(self):
        distribution = residual.empirical_distribution(100, 100, [-0.1, 0, 0.1, 0.2])
        self.assertEqual(distribution["probability_above_current"], 0.5)
        self.assertAlmostEqual(distribution["lower_95"], 100 * math.exp(-0.0925))
        self.assertAlmostEqual(distribution["upper_95"], 100 * math.exp(0.1925))
        self.assertLessEqual(distribution["lower_95"], distribution["lower_90"])
        self.assertLessEqual(distribution["upper_90"], distribution["upper_95"])
        flat = residual.empirical_distribution(100, 100, [0, 0])
        self.assertEqual(flat["probability_above_current"], 0)
        self.assertEqual(flat["lower_95"], 100)
        for price, values in ((0, [0, 0]), (100, [0]), (100, [0, float("nan")]),
                              (100, [1000, 1000]), (100, [-1000, -1000])):
            with self.assertRaises(ValueError):
                residual.empirical_distribution(price, 100, values)

    def test_interval_score_penalizes_misses(self):
        points = [{"actual_price": 120, "current_price": 100, "distributions": {
            "ensemble": {"probability_above_current": 0.75,
                         **{f"{side}_{level}": value for level in (80, 90, 95)
                            for side, value in (("lower", 90), ("upper", 110))}}}}]
        metrics = residual.distribution_metrics(points, "ensemble")
        self.assertEqual(metrics["brier_score"], 0.0625)
        self.assertEqual(metrics["coverage_95"], 0)
        self.assertEqual(metrics["mean_width_95"], 20)
        self.assertAlmostEqual(metrics["interval_score_95"], 420)
        points[0]["actual_price"] = 80
        self.assertAlmostEqual(residual.distribution_metrics(points, "ensemble")["interval_score_95"], 420)
        points[0]["actual_price"] = 100
        self.assertEqual(residual.distribution_metrics(points, "ensemble")["interval_score_95"], 20)

    def test_paired_evaluation_and_calibration_boundaries(self):
        report = residual.evaluate(self.manifest)
        self.assertEqual(report, residual.evaluate(self.manifest))
        self.assertEqual(len(report["results"]), 6)
        self.assertEqual(report["phase"], "validation")
        for group in report["results"]:
            self.assertTrue(group["points"])
            for point in group["points"]:
                self.assertLessEqual(point["last_calibration_target"], point["origin_date"])
                self.assertLessEqual(point["first_calibration_target"], point["last_calibration_target"])
                self.assertTrue(20 <= point["calibration_count"] <= 60)
                origin = self.dates.index(point["origin_date"])
                self.assertEqual(point["last_calibration_target"], point["origin_date"])
                self.assertEqual(point["first_calibration_target"],
                                 self.dates[origin - (point["calibration_count"] - 1) * group["horizon_days"]])
                self.assertLess(point["target_date"], self.config["holdout_start"])
                self.assertGreaterEqual(point["origin_date"], self.config["validation_start"])
                self.assertEqual(set(point["distributions"]), {"ensemble", "latest_price"})
            if group["horizon_days"] == 1:
                self.assertTrue(all(p["calibration_count"] == 60 for p in group["points"]))
            if group["symbol"] == "FLAT":
                for name in ("ensemble", "latest_price"):
                    metrics = group["metrics"][name]
                    self.assertEqual(metrics["point"]["mae"], 0)
                    self.assertEqual(metrics["distribution"]["coverage_95"], 1)
                    self.assertEqual(metrics["distribution"]["mean_width_95"], 0)
                    self.assertEqual(metrics["distribution"]["brier_score"], 0)

    def test_future_mutations_do_not_change_earlier_ranges(self):
        original = residual.evaluate(self.manifest)
        asset = self.config["assets"][1]
        history = self.directory / asset["csv"]
        with history.open() as stream:
            rows = list(csv.reader(stream))
        for index, row in enumerate(rows[1:]):
            if index >= 660:
                row[1] = str(float(row[1]) * 2)
        with history.open("w", newline="") as stream:
            csv.writer(stream).writerows(rows)
        asset["csv_sha256"] = residual.sha256(history.read_bytes())
        self.save()
        self.assertEqual(original["results"], residual.evaluate(self.manifest)["results"])
        for index, row in enumerate(rows[1:]):
            if index >= 550:
                row[1] = str(float(row[1]) * 2)
        with history.open("w", newline="") as stream:
            csv.writer(stream).writerows(rows)
        asset["csv_sha256"] = residual.sha256(history.read_bytes())
        self.save()
        changed = residual.evaluate(self.manifest)
        for before, after in zip(original["results"], changed["results"]):
            for first, second in zip(before["points"], after["points"]):
                if first["origin_date"] < self.dates[550]:
                    self.assertEqual(first["forecast_price"], second["forecast_price"])
                    self.assertEqual(first["distributions"], second["distributions"])

    def test_cli_output_protection_and_insufficient_history(self):
        output = self.directory / "ranges.json"
        command = [sys.executable, str(ROOT / "python/residual_forecasts.py"),
                   "--manifest", str(self.manifest), "--output", str(output)]
        process = subprocess.run(command, capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(output.read_text())
        self.assertEqual(report["phase"], "validation")
        prior = output.read_bytes()
        self.assertNotEqual(subprocess.run(command, capture_output=True).returncode, 0)
        self.assertEqual(prior, output.read_bytes())
        self.config["holdout_start"] = self.dates[300]
        self.config["validation_start"] = self.dates[100]
        self.save()
        with self.assertRaisesRegex(ValueError, "warmup"):
            residual.evaluate(self.manifest)
        with self.assertRaises(ValueError):
            residual.rolling_points([], [], 0, 60, self.dates[0])


if __name__ == "__main__":
    unittest.main()
