import csv
import importlib.util
import json
import math
import os
import statistics
import subprocess
import sys
import tempfile
import unittest
from datetime import date, timedelta
from pathlib import Path
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("benchmark_forecasts", ROOT / "python/benchmark_forecasts.py")
benchmark = importlib.util.module_from_spec(spec)
spec.loader.exec_module(benchmark)


class BenchmarkTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.directory = Path(self.temporary.name)
        day = date(2020, 1, 1)
        self.dates = []
        while len(self.dates) < 180:
            if day.weekday() < 5:
                self.dates.append(day.isoformat())
            day += timedelta(days=1)
        self.prices = [100 * math.exp(0.0003 * index + 0.01 * math.sin(index)) for index in range(180)]
        self.config = {"schema_version": 1, "lookback_days": 10,
                       "bootstrap": {"samples": 100, "seed": 42, "block_sizes": [1, 2, 4]},
                       "validation_start": self.dates[20], "holdout_start": self.dates[100],
                       "test_end": self.dates[-1], "assets": []}
        for symbol, prices in (("SYNTH_A", self.prices), ("SYNTH_B", [100.0] * 180)):
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
                "metadata": metadata.name, "csv_sha256": benchmark.sha256(history.read_bytes()),
                "metadata_sha256": benchmark.sha256(metadata.read_bytes())})
        self.manifest = self.directory / "manifest.json"
        self.save()
        self.engine = Path(os.environ.get("MC_ENGINE", ROOT / "build/mcprice")).resolve()

    def save(self):
        self.manifest.write_text(json.dumps(self.config), encoding="utf-8")

    def test_manifest_rejects_ambiguous_or_unfrozen_settings(self):
        for key, value in (("lookback_days", True), ("lookback_days", 1),
                           ("validation_start", "20200101"), ("test_end", "2019-01-01"),
                           ("schema_version", 2), ("assets", self.config["assets"][:1])):
            original = self.config[key]
            self.config[key] = value
            self.save()
            with self.assertRaises(ValueError):
                benchmark.read_manifest(self.manifest)
            self.config[key] = original
        self.manifest.write_text('{"schema_version":1,"schema_version":1}', encoding="utf-8")
        with self.assertRaises(ValueError):
            benchmark.read_manifest(self.manifest)

    def test_checksums_reject_changed_history_and_metadata(self):
        for key in ("csv", "metadata"):
            path = self.directory / self.config["assets"][0][key]
            original = path.read_bytes()
            path.write_bytes(original + b"\n")
            with self.assertRaisesRegex(ValueError, "checksum"):
                benchmark.read_asset(self.config["assets"][0], self.directory)
            path.write_bytes(original)

    def test_summary_treats_unchanged_actual_as_neither_up_nor_down(self):
        points = [{"current_price": 100, "forecast_price": 99, "actual_price": 100,
                   "latest_price_forecast": 100, "probability_above_current": 0.4,
                   "lower_95": 95, "upper_95": 105}]
        result = benchmark.summarize(points)
        self.assertEqual(result["selected"]["directional_accuracy"], 0)
        self.assertIsNone(result["latest_price"]["directional_accuracy"])
        self.assertIsNone(result["relative_mae_improvement"])
        self.assertAlmostEqual(result["brier_score"], 0.16)
        self.assertEqual(result["always_up_accuracy"], 0)
        self.assertEqual(result["always_up_brier_score"], 1)
        self.assertEqual(result["half_brier_score"], 0.25)
        self.assertIsNone(result["selected"]["mase"])
        with self.assertRaises(ValueError):
            benchmark.summarize([])

    def test_scaled_errors_and_probability_baselines_have_known_values(self):
        points = [{"current_price": 100, "forecast_price": 102, "actual_price": 104,
                   "latest_price_forecast": 100, "probability_above_current": 0.75,
                   "lower_95": 95, "upper_95": 105, "naive_error_scale": 2,
                   "historical_up_probability": 0.5}]
        result = benchmark.summarize(points)
        self.assertEqual(result["selected"]["mase"], 1)
        self.assertEqual(result["latest_price"]["mase"], 2)
        self.assertEqual(result["historical_up_brier_score"], 0.25)
        self.assertEqual(result["always_up_accuracy"], 1)
        self.assertEqual(result["always_up_brier_score"], 0)

    def test_bootstrap_is_deterministic_and_does_not_infer_from_tiny_samples(self):
        points = [{"latest_price_forecast": 100, "forecast_price": 101, "actual_price": 102}] * 40
        settings = {"samples": 100, "seed": 42, "block_sizes": [1, 2, 8]}
        result = benchmark.bootstrap_sensitivity(points, settings)
        self.assertEqual(result, benchmark.bootstrap_sensitivity(points, settings))
        self.assertEqual(result[0]["lower_95"], 1)
        self.assertEqual(result[0]["conclusion"], "better")
        self.assertIsNone(result[-1]["lower_95"])
        self.assertEqual(result[-1]["conclusion"], "inconclusive")

    def test_manifest_rejects_invalid_bootstrap_settings(self):
        for settings in ({"samples": 99, "seed": 42, "block_sizes": [1]},
                         {"samples": 100, "seed": -1, "block_sizes": [1]},
                         {"samples": 100, "seed": 42, "block_sizes": [0]},
                         {"samples": 100, "seed": 42, "block_sizes": [1, 1]}):
            self.config["bootstrap"] = settings
            self.save()
            with self.assertRaises(ValueError):
                benchmark.read_manifest(self.manifest)

    def test_validation_excludes_holdout_from_engine_inputs_and_matches_reference(self):
        real_run = subprocess.run

        def inspect(command, **kwargs):
            history = Path(command[command.index("--csv") + 1])
            with history.open(newline="") as stream:
                rows = list(csv.DictReader(stream))
            self.assertTrue(all(row["Date"] < self.config["holdout_start"] for row in rows))
            return real_run(command, **kwargs)

        with patch.object(benchmark.subprocess, "run", side_effect=inspect):
            report = benchmark.run_benchmark(self.manifest, self.engine, "validation")
        self.assertEqual(len(report["results"]), 6)
        self.assertEqual(report, benchmark.run_benchmark(self.manifest, self.engine, "validation"))
        for result in report["results"]:
            horizon = result["horizon_days"]
            self.assertLess(result["last_target"], self.config["holdout_start"])
            if result["symbol"] == "SYNTH_B":
                self.assertEqual(result["metrics"]["selected"]["mae"], 0)
                self.assertIsNone(result["metrics"]["selected"]["directional_accuracy"])
                continue
            errors = []
            for point in result["points"]:
                origin = self.dates.index(point["origin_date"])
                training = self.prices[origin - 10:origin + 1]
                self.assertAlmostEqual(point["naive_error_scale"], statistics.mean(
                    abs(b - a) for a, b in zip(training, training[1:])), places=12)
                expected_probability = (statistics.mean(
                    training[index] > training[index - horizon]
                    for index in range(horizon, len(training))) if horizon <= 10 else None)
                if expected_probability is None:
                    self.assertIsNone(point["historical_up_probability"])
                else:
                    self.assertAlmostEqual(point["historical_up_probability"], expected_probability, places=14)
                returns = [math.log(b) - math.log(a) for a, b in zip(training, training[1:])]
                forecast = training[-1] * math.exp(horizon * (
                    statistics.mean(returns) + statistics.variance(returns) / 2))
                self.assertAlmostEqual(point["forecast_price"], forecast, places=11)
                errors.append(abs(forecast - point["actual_price"]))
            self.assertAlmostEqual(result["metrics"]["selected"]["mae"], statistics.mean(errors), places=11)

    def test_holdout_is_explicit_and_stays_inside_test_dates(self):
        report = benchmark.run_benchmark(self.manifest, self.engine, "holdout")
        for result in report["results"]:
            self.assertGreaterEqual(result["first_origin"], self.config["holdout_start"])
            self.assertLessEqual(result["last_target"], self.config["test_end"])
            points = result["points"]
            for previous, following in zip(points, points[1:]):
                self.assertGreaterEqual(following["origin_date"], previous["target_date"])

    def test_cli_preserves_existing_output(self):
        output = self.directory / "result.json"
        output.write_text("keep me", encoding="utf-8")
        result = subprocess.run([sys.executable, str(ROOT / "python/benchmark_forecasts.py"),
                                 "--manifest", str(self.manifest), "--engine", str(self.engine),
                                 "--output", str(output)], capture_output=True, text=True)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(output.read_text(), "keep me")

    def test_cli_success_saves_default_validation_and_provenance(self):
        output = self.directory / "validation.json"
        process = subprocess.run([sys.executable, str(ROOT / "python/benchmark_forecasts.py"),
                                 "--manifest", str(self.manifest), "--engine", str(self.engine),
                                 "--output", str(output)], capture_output=True, text=True)
        self.assertEqual(process.returncode, 0, process.stderr)
        report = json.loads(output.read_text())
        self.assertEqual(report["phase"], "validation")
        self.assertEqual(report["engine_sha256"], benchmark.sha256(self.engine.read_bytes()))
        self.assertEqual(report["manifest_sha256"], benchmark.sha256(self.manifest.read_bytes()))
        self.assertEqual(len(report["results"]), 6)

    def test_changes_in_holdout_prices_cannot_change_validation_predictions(self):
        original = benchmark.run_benchmark(self.manifest, self.engine, "validation")
        history = self.directory / self.config["assets"][0]["csv"]
        with history.open(newline="") as stream:
            rows = list(csv.DictReader(stream))
        for row in rows:
            if row["Date"] >= self.config["holdout_start"]:
                row["Adj Close"] = str(float(row["Adj Close"]) * 2)
        with history.open("w", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=["Date", "Adj Close"])
            writer.writeheader()
            writer.writerows(rows)
        self.config["assets"][0]["csv_sha256"] = benchmark.sha256(history.read_bytes())
        self.save()
        changed = benchmark.run_benchmark(self.manifest, self.engine, "validation")
        self.assertEqual(original["results"], changed["results"])

    def test_symbol_mismatch_and_empty_splits_fail_loudly(self):
        self.config["assets"][0]["symbol"] = "WRONG"
        self.save()
        with self.assertRaisesRegex(ValueError, "symbol"):
            benchmark.run_benchmark(self.manifest, self.engine, "validation")
        self.config["assets"][0]["symbol"] = "SYNTH_A"
        self.config["validation_start"] = self.dates[99]
        self.save()
        with self.assertRaisesRegex(ValueError, "no eligible forecasts"):
            benchmark.run_benchmark(self.manifest, self.engine, "validation")


if __name__ == "__main__":
    unittest.main()
