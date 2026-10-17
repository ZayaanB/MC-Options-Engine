import argparse
import csv
import json
import math
import platform
import statistics
from pathlib import Path

from benchmark_forecasts import HORIZONS, read_asset, read_manifest, root_mean_square, sha256


def ridge_predict(features, targets, current, penalty=1.0):
    if not math.isfinite(penalty) or penalty <= 0:
        raise ValueError("ridge penalty must be finite and positive")
    if len(features) != len(targets) or len(features) < 2 or not current:
        raise ValueError("ridge requires matching training rows and targets")
    width = len(current)
    if any(len(row) != width for row in features):
        raise ValueError("inconsistent feature dimensions")
    if any(not math.isfinite(value) for row in [*features, targets, current] for value in row):
        raise ValueError("ridge inputs must be finite")
    means = [statistics.mean(row[j] for row in features) for j in range(width)]
    scales = [statistics.pstdev(row[j] for row in features) or 1.0 for j in range(width)]
    rows = [[(value - mean) / scale for value, mean, scale in zip(row, means, scales)]
            for row in features]
    center = statistics.mean(targets)
    matrix = [[math.fsum(row[j] * row[k] for row in rows) + (penalty if j == k else 0)
               for k in range(width)] for j in range(width)]
    rhs = [math.fsum(row[j] * (target - center) for row, target in zip(rows, targets))
           for j in range(width)]
    for j in range(width):
        pivot = max(range(j, width), key=lambda k: abs(matrix[k][j]))
        matrix[j], matrix[pivot] = matrix[pivot], matrix[j]
        rhs[j], rhs[pivot] = rhs[pivot], rhs[j]
        if not math.isfinite(matrix[j][j]) or matrix[j][j] == 0:
            raise ValueError("ridge system is numerically singular")
        for k in range(j + 1, width):
            factor = matrix[k][j] / matrix[j][j]
            for column in range(j + 1, width):
                matrix[k][column] -= factor * matrix[j][column]
            rhs[k] -= factor * rhs[j]
    weights = [0.0] * width
    for j in reversed(range(width)):
        weights[j] = (rhs[j] - math.fsum(matrix[j][k] * weights[k]
                      for k in range(j + 1, width))) / matrix[j][j]
    prediction = center + math.fsum(weight * (value - mean) / scale
                                  for weight, value, mean, scale in zip(weights, current, means, scales))
    if not math.isfinite(prediction):
        raise ValueError("ridge prediction exceeds finite range")
    return prediction


def forecast(prices, horizon, lookback):
    if type(horizon) is not int or horizon <= 0 or type(lookback) is not int or lookback < 20:
        raise ValueError("require positive horizon and lookback of at least 20")
    if any(not math.isfinite(price) or price <= 0 for price in prices):
        raise ValueError("prices must be finite and positive")
    origin = len(prices) - 1
    logs = [math.log(price) for price in prices]
    returns = [logs[i] - logs[i - 1] for i in range(1, len(logs))]

    def feature(index):
        return [returns[index - 1], (logs[index] - logs[index - 5]) / 5,
                (logs[index] - logs[index - 20]) / 20]

    indices = range(max(20, origin - lookback), origin - horizon + 1)
    if len(indices) < 20:
        raise ValueError("insufficient history for 20 matured training labels")
    predicted_return = ridge_predict([feature(index) for index in indices],
                                    [logs[index + horizon] - logs[index] for index in indices],
                                    feature(origin))
    try:
        price = prices[-1] * math.exp(predicted_return)
    except OverflowError as error:
        raise ValueError("forecast price exceeds finite range") from error
    if not math.isfinite(price) or price <= 0:
        raise ValueError("forecast price exceeds positive finite range")
    return {"forecast_price": price, "predicted_log_return": predicted_return,
            "training_rows": len(indices), "last_training_target_index": origin}


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
            points = []
            first = max(config["lookback_days"], 20 + horizon + 19)
            first = next((i for i in range(first, len(prices) - horizon)
                          if dates[i] >= config["validation_start"]), len(prices))
            for origin in range(first, len(prices) - horizon, horizon):
                prediction = forecast(prices[:origin + 1], horizon, config["lookback_days"])
                points.append({"origin_date": dates[origin], "target_date": dates[origin + horizon],
                               "current_price": prices[origin], "actual_price": prices[origin + horizon],
                               **prediction})
            if not points:
                raise ValueError(f"{asset['symbol']} {horizon}d: no eligible forecasts")
            metrics = {}
            for name, key in (("ridge", "forecast_price"), ("latest_price", "current_price")):
                errors = [point[key] - point["actual_price"] for point in points]
                metrics[name] = {"mae": statistics.mean(abs(error) for error in errors),
                                 "rmse": root_mean_square(errors)}
            moving = [p for p in points if p["forecast_price"] != p["current_price"]]
            metrics["directional_predictions"] = len(moving)
            metrics["directional_accuracy"] = statistics.mean(
                ((p["forecast_price"] > p["current_price"]) - (p["forecast_price"] < p["current_price"])) ==
                ((p["actual_price"] > p["current_price"]) - (p["actual_price"] < p["current_price"]))
                for p in moving) if moving else None
            results.append({"symbol": asset["symbol"], "horizon_days": horizon,
                            "forecasts": len(points), "metrics": metrics, "points": points})
    return {"schema_version": 1, "phase": "validation", "manifest_sha256": digest,
            "source_sha256": sha256(Path(__file__).read_bytes()),
            "data_loader_sha256": sha256(Path(__file__).with_name("benchmark_forecasts.py").read_bytes()),
            "config": config, "provenance": provenance, "python_version": platform.python_version(),
            "platform": platform.platform(), "model": {"name": "ridge", "penalty": 1.0,
            "objective": "sum squared centered log-return errors + penalty * squared slopes",
            "features": ["latest_log_return", "mean_5_log_returns", "mean_20_log_returns"],
            "scaling": "training-only population standard deviation; unpenalized intercept"},
            "limitations": ["Point forecasts only; no calibrated probability or prediction interval.",
                            "Exponentiated fitted log return is not an expected arithmetic price.",
                            "Validation is exploratory; no holdout scored or predictive edge established.",
                            "Training labels overlap, and daily cadence is not exchange-calendar checked."],
            "results": results}


def main():
    parser = argparse.ArgumentParser(description="Validation-only rolling ridge return model")
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
        parser.exit(1, f"Ridge evaluation failed: {error}\n")
    for result in report["results"]:
        metric = result["metrics"]
        print(f"{result['symbol']} {result['horizon_days']}d: "
              f"ridge MAE={metric['ridge']['mae']:.6f}, latest price={metric['latest_price']['mae']:.6f}")


if __name__ == "__main__":
    main()
