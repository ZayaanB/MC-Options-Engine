import argparse
import csv
import hashlib
import io
import json
import math
import platform
import random
import shutil
import statistics
import subprocess
import tempfile
from datetime import date
from pathlib import Path


HORIZONS = (1, 5, 20)
METADATA_KEYS = {"provider", "symbol", "adjustment", "retrieved_on", "source_url",
                 "price_column", "frequency"}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def iso_date(value):
    if not isinstance(value, str) or date.fromisoformat(value).isoformat() != value:
        raise ValueError("dates must use YYYY-MM-DD")
    return value


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"duplicate manifest key: {key}")
        result[key] = value
    return result


def read_manifest(path):
    raw = path.read_bytes()
    config = json.loads(raw, object_pairs_hook=unique_object)
    required = {"schema_version", "lookback_days", "validation_start", "holdout_start",
                "test_end", "assets"}
    if not isinstance(config, dict) or not required <= set(config) or set(config) - required - {"bootstrap"}:
        raise ValueError("manifest must contain exactly the documented fields")
    if type(config["schema_version"]) is not int or config["schema_version"] != 1:
        raise ValueError("unsupported manifest schema_version")
    if type(config["lookback_days"]) is not int or config["lookback_days"] < 2:
        raise ValueError("lookback_days must be an integer of at least two")
    start, holdout, end = (iso_date(config[key]) for key in
                           ("validation_start", "holdout_start", "test_end"))
    if not start < holdout <= end:
        raise ValueError("require validation_start < holdout_start <= test_end")
    bootstrap = config.get("bootstrap", {"samples": 2000, "seed": 42, "block_sizes": [1, 2, 4]})
    if not isinstance(bootstrap, dict) or set(bootstrap) != {"samples", "seed", "block_sizes"}:
        raise ValueError("bootstrap requires samples, seed and block_sizes")
    if type(bootstrap["samples"]) is not int or bootstrap["samples"] < 100:
        raise ValueError("bootstrap samples must be at least 100")
    if type(bootstrap["seed"]) is not int or bootstrap["seed"] < 0:
        raise ValueError("bootstrap seed must be a nonnegative integer")
    blocks = bootstrap["block_sizes"]
    if (not isinstance(blocks, list) or not blocks or
            any(type(block) is not int or block <= 0 for block in blocks) or len(set(blocks)) != len(blocks)):
        raise ValueError("bootstrap block_sizes must be distinct positive integers")
    assets = config["assets"]
    if not isinstance(assets, list) or len(assets) < 2:
        raise ValueError("freeze at least two assets before benchmarking")
    symbols = set()
    for asset in assets:
        if not isinstance(asset, dict) or set(asset) != {
                "symbol", "csv", "metadata", "csv_sha256", "metadata_sha256"}:
            raise ValueError("each asset requires symbol, csv, metadata and both SHA256 hashes")
        if any(not isinstance(value, str) or not value.strip() for value in asset.values()):
            raise ValueError("asset fields must be nonempty strings")
        if asset["symbol"] in symbols:
            raise ValueError("duplicate asset symbol")
        symbols.add(asset["symbol"])
        for key in ("csv_sha256", "metadata_sha256"):
            digest = asset[key]
            if len(digest) != 64 or any(char not in "0123456789abcdef" for char in digest):
                raise ValueError(f"{key} must be a lowercase SHA256 digest")
    return config, sha256(raw)


def read_asset(asset, directory):
    raw = (directory / asset["csv"]).read_bytes()
    metadata_raw = (directory / asset["metadata"]).read_bytes()
    if sha256(raw) != asset["csv_sha256"] or sha256(metadata_raw) != asset["metadata_sha256"]:
        raise ValueError(f"{asset['symbol']}: input checksum mismatch")
    metadata = {}
    for line in metadata_raw.decode("utf-8").split("\n"):
        line = line.removesuffix("\r")
        if not line:
            continue
        key, separator, value = line.partition("=")
        if not separator or key not in METADATA_KEYS or key in metadata or not value.strip():
            raise ValueError("invalid, duplicate or empty metadata field")
        metadata[key] = value
    if set(metadata) != METADATA_KEYS or metadata["frequency"] != "daily":
        raise ValueError("metadata requires all seven keys and daily frequency")
    if metadata["symbol"] != asset["symbol"]:
        raise ValueError("manifest symbol does not match metadata")
    iso_date(metadata["retrieved_on"])
    lines = raw.decode("utf-8-sig").split("\n")
    header = next(csv.reader([lines[0].removesuffix("\r")], strict=True))
    column = metadata["price_column"]
    if header.count("Date") != 1 or header.count(column) != 1:
        raise ValueError("CSV requires unique Date and selected price headers")
    date_index, price_index = header.index("Date"), header.index(column)
    rows = []
    for line in lines[1:]:
        line = line.removesuffix("\r")
        if not line:
            continue
        fields = next(csv.reader([line], strict=True))
        if len(fields) != len(header):
            raise ValueError("CSV row has unexpected column count")
        day = iso_date(fields[date_index])
        price = float(fields[price_index])
        if not math.isfinite(price) or price <= 0:
            raise ValueError("CSV prices must be finite and positive")
        if rows and day <= rows[-1][0]:
            raise ValueError("CSV dates must be strictly increasing")
        rows.append((day, line))
    if not rows or metadata["retrieved_on"] < rows[-1][0]:
        raise ValueError("empty history or retrieval date before history ends")
    return lines[0].removesuffix("\r"), rows, metadata, metadata_raw


def summarize(points):
    if not points:
        raise ValueError("split has no eligible forecasts; extend history or adjust split dates")
    result = {}
    for name, column in (("selected", "forecast_price"), ("latest_price", "latest_price_forecast")):
        errors = [point[column] - point["actual_price"] for point in points]
        directional = [point for point in points if point[column] != point["current_price"]]
        accuracy = (statistics.mean(
            ((point[column] > point["current_price"]) - (point[column] < point["current_price"])) ==
            ((point["actual_price"] > point["current_price"]) -
             (point["actual_price"] < point["current_price"])) for point in directional)
            if directional else None)
        result[name] = {
            "mae": statistics.mean(abs(error) for error in errors),
            "rmse": math.sqrt(statistics.mean(error * error for error in errors)),
            "mape": statistics.mean(abs(error) / point["actual_price"]
                                    for error, point in zip(errors, points)),
            "directional_predictions": len(directional), "directional_accuracy": accuracy}
        scales = [point.get("naive_error_scale") for point in points]
        result[name]["mase"] = (statistics.mean(abs(error) / scale for error, scale in zip(errors, scales))
                                if all(scale is not None and scale > 0 for scale in scales) else None)
    result["brier_score"] = statistics.mean(
        (point["probability_above_current"] -
         (point["actual_price"] > point["current_price"])) ** 2 for point in points)
    result["always_up_accuracy"] = statistics.mean(
        point["actual_price"] > point["current_price"] for point in points)
    result["half_brier_score"] = 0.25
    result["always_up_brier_score"] = 1.0 - result["always_up_accuracy"]
    probabilities = [point.get("historical_up_probability") for point in points]
    result["historical_up_brier_score"] = (statistics.mean(
        (probability - (point["actual_price"] > point["current_price"])) ** 2
        for probability, point in zip(probabilities, points))
        if all(probability is not None for probability in probabilities) else None)
    result["coverage_95"] = statistics.mean(
        point["lower_95"] <= point["actual_price"] <= point["upper_95"] for point in points)
    result["mean_width_95"] = statistics.mean(point["upper_95"] - point["lower_95"] for point in points)
    baseline = result["latest_price"]["mae"]
    result["relative_mae_improvement"] = (
        (baseline - result["selected"]["mae"]) / baseline if baseline else None)
    numbers = [value for metrics in result.values() if isinstance(metrics, dict)
               for value in metrics.values() if value is not None]
    numbers.extend(value for value in result.values() if isinstance(value, (int, float)))
    if any(not math.isfinite(value) for value in numbers):
        raise ValueError("benchmark metrics exceed finite numeric range")
    return result


def bootstrap_sensitivity(points, settings):
    improvements = [abs(point["latest_price_forecast"] - point["actual_price"]) -
                    abs(point["forecast_price"] - point["actual_price"]) for point in points]
    count = len(improvements)
    results = []
    for block in settings["block_sizes"]:
        result = {"block_size": block, "effective_blocks": count // block,
                  "lower_95": None, "upper_95": None, "conclusion": "inconclusive"}
        if count // block >= 10:
            generator = random.Random(settings["seed"])
            estimates = []
            for _ in range(settings["samples"]):
                sample = []
                while len(sample) < count:
                    start = generator.randrange(count)
                    take = min(block, count - len(sample))
                    sample.extend(improvements[(start + offset) % count] for offset in range(take))
                estimates.append(math.fsum(sample) / count)
            estimates.sort()

            def quantile(probability):
                position = probability * (len(estimates) - 1)
                lower = math.floor(position)
                upper = math.ceil(position)
                return estimates[lower] + (position - lower) * (estimates[upper] - estimates[lower])

            result["lower_95"], result["upper_95"] = quantile(0.025), quantile(0.975)
            if result["lower_95"] > 0:
                result["conclusion"] = "better"
            elif result["upper_95"] < 0:
                result["conclusion"] = "worse"
        results.append(result)
    return results


def run_benchmark(manifest, engine, phase):
    if phase not in {"validation", "holdout"}:
        raise ValueError("phase must be validation or holdout")
    config, manifest_hash = read_manifest(manifest)
    assets = [(asset, read_asset(asset, manifest.parent)) for asset in config["assets"]]
    engine_raw = engine.read_bytes()
    results = []
    with tempfile.TemporaryDirectory(prefix="mc-forecast-benchmark-") as temporary:
        workspace = Path(temporary)
        executable = workspace / "mcprice"
        shutil.copyfile(engine, executable)
        executable.chmod(0o700)
        if sha256(executable.read_bytes()) != sha256(engine_raw):
            raise ValueError("engine changed while creating benchmark snapshot")
        for index, (asset, (header, rows, metadata, metadata_raw)) in enumerate(assets):
            retained = [(day, line) for day, line in rows
                        if day <= config["test_end"] and
                        (phase == "holdout" or day < config["holdout_start"])]
            history = workspace / f"history_{index}.csv"
            provenance = workspace / f"history_{index}.meta"
            history.write_text(header + "\n" + "\n".join(line for _, line in retained) + "\n",
                               encoding="utf-8")
            provenance.write_bytes(metadata_raw)
            for horizon in HORIZONS:
                command = [str(executable), "backtest", "--csv", str(history),
                           "--metadata", str(provenance), "--price-column", metadata["price_column"],
                           "--lookback-days", str(config["lookback_days"]),
                           "--horizon-days", str(horizon), "--step-days", str(horizon),
                           "--bootstrap-samples", "100", "--format", "csv"]
                try:
                    process = subprocess.run(command, capture_output=True, text=True, check=True, timeout=300)
                except subprocess.CalledProcessError as error:
                    raise ValueError(f"{asset['symbol']} {horizon}d: {error.stderr.strip()}") from error
                points = []
                start = config["holdout_start"] if phase == "holdout" else config["validation_start"]
                for row in csv.DictReader(io.StringIO(process.stdout)):
                    if row["origin_date"] < start:
                        continue
                    if phase == "validation" and row["target_date"] >= config["holdout_start"]:
                        continue
                    point = {key: value if key.endswith("_date") else (float(value) if value else None)
                             for key, value in row.items()}
                    if any(not math.isfinite(value) for value in point.values() if isinstance(value, float)):
                        raise ValueError("engine returned nonfinite forecast points")
                    points.append(point)
                metrics = summarize(points)
                bootstrap_settings = config.get("bootstrap", {"samples": 2000, "seed": 42,
                                                               "block_sizes": [1, 2, 4]})
                sensitivity = bootstrap_sensitivity(points, bootstrap_settings)
                results.append({"symbol": asset["symbol"], "horizon_days": horizon,
                                "step_days": horizon, "forecasts": len(points),
                                "first_origin": points[0]["origin_date"],
                                "last_target": points[-1]["target_date"],
                                "metrics": metrics, "points": points,
                                "bootstrap_sensitivity": sensitivity})
    return {"schema_version": 1, "phase": phase, "manifest_sha256": manifest_hash,
            "engine_sha256": sha256(engine_raw), "python_version": platform.python_version(),
            "platform": platform.platform(), "config": config,
            "model": {"drift": "historical", "volatility": "sample", "trading_days": 252},
            "provenance": [metadata for _, (_, _, metadata, _) in assets],
            "bootstrap": config.get("bootstrap", {"samples": 2000, "seed": 42, "block_sizes": [1, 2, 4]}),
            "limitations": ["No exchange-calendar validation; metadata is declared, not verified.",
                            "Bootstrap intervals depend on chosen blocks; no predictive-edge claim.",
                            "Nonoverlapping targets can still have dependent errors and shared training data.",
                            "Re-running or tuning on holdout results invalidates its untouched status."],
            "results": results}


def main():
    parser = argparse.ArgumentParser(description="Frozen multi-stock walk-forward benchmark")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--engine", type=Path, default=Path("build/mcprice"))
    parser.add_argument("--phase", choices=("validation", "holdout"), default="validation")
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.output.exists():
            raise ValueError("output already exists; preserve prior runs and choose a new path")
        report = run_benchmark(arguments.manifest.resolve(), arguments.engine.resolve(), arguments.phase)
        encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
        with arguments.output.open("x", encoding="utf-8") as output:
            output.write(encoded)
    except (ValueError, TypeError, KeyError, OSError, csv.Error, subprocess.SubprocessError) as error:
        parser.exit(1, f"Benchmark failed: {error}\n")
    for result in report["results"]:
        metrics = result["metrics"]
        print(f"{result['symbol']} {result['horizon_days']}d: n={result['forecasts']}, "
              f"MAE={metrics['selected']['mae']:.6f}, baseline={metrics['latest_price']['mae']:.6f}")
    print(f"Saved {arguments.phase} results to {arguments.output}")


if __name__ == "__main__":
    main()
