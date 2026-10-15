import argparse
import csv
import json
import math
import platform
import statistics
from collections import deque
from pathlib import Path

from benchmark_forecasts import HORIZONS, read_asset, read_manifest, sha256
from ensemble_forecasts import blend, point_metrics
from ridge_forecasts import forecast


def empirical_distribution(price, current, residuals):
    if len(residuals) < 2 or any(not math.isfinite(value) for value in residuals):
        raise ValueError("require at least two finite residuals")
    if any(not math.isfinite(value) or value <= 0 for value in (price, current)):
        raise ValueError("distribution requires positive finite prices")
    ordered = sorted(residuals)

    def quantile(probability):
        position = probability * (len(ordered) - 1)
        lower, upper = math.floor(position), math.ceil(position)
        value = ordered[lower] + (position - lower) * (ordered[upper] - ordered[lower])
        try:
            bound = price * math.exp(value)
        except OverflowError as error:
            raise ValueError("empirical interval exceeds finite range") from error
        if not math.isfinite(bound) or bound <= 0:
            raise ValueError("empirical interval exceeds positive finite range")
        return bound

    result = {"probability_above_current": statistics.mean(
              value > math.log(current) - math.log(price) for value in ordered)}
    for level in (80, 90, 95):
        tail = (1 - level / 100) / 2
        result[f"lower_{level}"] = quantile(tail)
        result[f"upper_{level}"] = quantile(1 - tail)
    return result


def rolling_points(dates, prices, horizon, lookback, start, minimum=20, maximum=60):
    if (type(horizon) is not int or horizon <= 0 or type(lookback) is not int or lookback < 20 or
            type(minimum) is not int or minimum < 2 or type(maximum) is not int or maximum < minimum):
        raise ValueError("invalid horizon, lookback or calibration window")
    if len(dates) != len(prices):
        raise ValueError("dates and prices must have matching lengths")
    errors = {name: deque(maxlen=maximum) for name in ("ensemble", "latest_price")}
    targets = deque(maxlen=maximum)
    previous = None
    points = []
    for origin in range(max(lookback, 39 + horizon), len(prices) - horizon, horizon):
        if previous is not None:
            for name in errors:
                errors[name].append(math.log(prices[origin]) - math.log(previous[name]))
            targets.append(dates[origin])
        ridge = forecast(prices[:origin + 1], horizon, lookback)
        selected = blend(prices[origin], ridge["forecast_price"])
        previous = {"ensemble": selected, "latest_price": prices[origin]}
        if len(targets) < minimum or dates[origin] < start:
            continue
        distributions = {name: empirical_distribution(previous[name], prices[origin], errors[name])
                         for name in errors}
        points.append({"origin_date": dates[origin], "target_date": dates[origin + horizon],
                       "current_price": prices[origin], "actual_price": prices[origin + horizon],
                       "forecast_price": selected, "ridge_price": ridge["forecast_price"],
                       "calibration_count": len(targets), "first_calibration_target": targets[0],
                       "last_calibration_target": targets[-1], "distributions": distributions})
    if not points:
        raise ValueError("no eligible forecasts after residual warmup; extend history")
    return points


def distribution_metrics(points, name):
    result = {"brier_score": statistics.mean(
        (p["distributions"][name]["probability_above_current"] -
         (p["actual_price"] > p["current_price"])) ** 2 for p in points), "half_brier_score": 0.25}
    for level in (80, 90, 95):
        widths, scores, coverage = [], [], []
        alpha = 1 - level / 100
        for point in points:
            bounds = point["distributions"][name]
            lower, upper = bounds[f"lower_{level}"], bounds[f"upper_{level}"]
            actual = point["actual_price"]
            width = upper - lower
            widths.append(width)
            scores.append(width + 2 / alpha * max(lower - actual, actual - upper, 0))
            coverage.append(lower <= actual <= upper)
        result[f"coverage_{level}"] = statistics.mean(coverage)
        result[f"mean_width_{level}"] = statistics.mean(widths)
        result[f"interval_score_{level}"] = statistics.mean(scores)
    if any(not math.isfinite(value) for value in result.values()):
        raise ValueError("distribution metrics exceed finite range")
    return result


def evaluate(manifest):
    config, digest = read_manifest(manifest)
    results, provenance = [], []
    for asset in config["assets"]:
        header, rows, metadata, _ = read_asset(asset, manifest.parent)
        column = next(csv.reader([header])).index(metadata["price_column"])
        retained = [(day, float(next(csv.reader([line]))[column])) for day, line in rows
                    if day < config["holdout_start"] and day <= config["test_end"]]
        dates, prices = zip(*retained) if retained else ((), ())
        provenance.append(metadata)
        for horizon in HORIZONS:
            points = rolling_points(dates, prices, horizon, config["lookback_days"], config["validation_start"])
            metrics = {name: {"point": point_metrics(points, key),
                             "distribution": distribution_metrics(points, name)}
                       for name, key in (("ensemble", "forecast_price"), ("latest_price", "current_price"))}
            results.append({"symbol": asset["symbol"], "horizon_days": horizon,
                            "step_days": horizon, "forecasts": len(points), "metrics": metrics, "points": points})
    return {"schema_version": 1, "phase": "validation", "manifest_sha256": digest, "config": config,
            "provenance": provenance, "python_version": platform.python_version(), "platform": platform.platform(),
            "source_hashes": {name: sha256(Path(__file__).with_name(name).read_bytes()) for name in
                              ("residual_forecasts.py", "ridge_forecasts.py", "ensemble_forecasts.py", "benchmark_forecasts.py")},
            "model": {"point": "50/50 latest-price/ridge ensemble", "ridge_penalty": 1.0,
                      "features": ["latest_log_return", "mean_5_log_returns", "mean_20_log_returns"],
                      "residual": "log(actual) - log(out-of-sample forecast)",
                      "minimum_residuals": 20, "maximum_residuals": 60,
                      "quantile": "linear interpolation at probability * (count - 1)",
                      "probability": "strict empirical exceedance frequency, without smoothing"},
            "limitations": ["Nominal empirical ranges, not guaranteed coverage or conformal intervals.",
                            "Twenty to sixty errors poorly resolve tails and may miss future regimes.",
                            "Nonoverlapping targets still share training data and can have dependent errors.",
                            "Empirical rise frequencies are not independently calibrated probabilities.",
                            "Validation only; no holdout scored, no predictive-edge claim.",
                            "Warmup and grid anchoring change dates versus earlier reports; compare only paired rows."],
            "results": results}


def main():
    parser = argparse.ArgumentParser(description="Validation-only out-of-sample residual forecast ranges")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.output.exists():
            raise ValueError("output already exists; choose a new path")
        report = evaluate(arguments.manifest.resolve())
        encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
        with arguments.output.open("x", encoding="utf-8") as stream:
            stream.write(encoded)
    except (ValueError, TypeError, KeyError, OSError, csv.Error, OverflowError) as error:
        parser.exit(1, f"Residual evaluation failed: {error}\n")
    for group in report["results"]:
        metrics = group["metrics"]
        print(f"{group['symbol']} {group['horizon_days']}d: n={group['forecasts']}, "
              f"95% coverage ensemble={metrics['ensemble']['distribution']['coverage_95']:.3f}, "
              f"latest price={metrics['latest_price']['distribution']['coverage_95']:.3f}")


if __name__ == "__main__":
    main()
