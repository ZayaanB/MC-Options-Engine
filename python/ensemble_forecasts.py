import argparse
import csv
import json
import math
import statistics
from pathlib import Path

from benchmark_forecasts import root_mean_square, sha256
from ridge_forecasts import evaluate as evaluate_ridge


def blend(latest, ridge):
    if any(not math.isfinite(value) or value <= 0 for value in (latest, ridge)):
        raise ValueError("ensemble inputs must be positive finite prices")
    return latest + (ridge - latest) * 0.5


def point_metrics(points, key):
    errors = [abs(point[key] - point["actual_price"]) for point in points]
    if not errors:
        raise ValueError("ensemble evaluation requires forecast points")
    moving = [point for point in points if point[key] != point["current_price"]]
    result = {"mae": statistics.mean(errors),
              "rmse": root_mean_square(errors),
              "directional_predictions": len(moving),
              "directional_accuracy": statistics.mean(
                  ((p[key] > p["current_price"]) - (p[key] < p["current_price"])) ==
                  ((p["actual_price"] > p["current_price"]) - (p["actual_price"] < p["current_price"]))
                  for p in moving) if moving else None}
    if any(not math.isfinite(value) for value in result.values() if value is not None):
        raise ValueError("ensemble metrics exceed finite numeric range")
    return result


def evaluate(manifest):
    report = evaluate_ridge(manifest)
    for group in report["results"]:
        for point in group["points"]:
            point["ridge_price"] = point.pop("forecast_price")
            point["ridge_predicted_log_return"] = point.pop("predicted_log_return")
            point["forecast_price"] = blend(point["current_price"], point["ridge_price"])
        group["metrics"] = {name: point_metrics(group["points"], key) for name, key in
                            (("ensemble", "forecast_price"), ("ridge", "ridge_price"),
                             ("latest_price", "current_price"))}
    ranking = [{"candidate": "latest_price", "mean_relative_mae": 1.0}]
    for name in ("ensemble", "ridge"):
        ratios = []
        disqualified = False
        for group in report["results"]:
            metrics = group["metrics"]
            baseline, error = metrics["latest_price"]["mae"], metrics[name]["mae"]
            if baseline:
                ratio = error / baseline
                if not math.isfinite(ratio):
                    raise ValueError("relative MAE exceeds finite numeric range")
                ratios.append(ratio)
            elif error:
                disqualified = True
        ranking.append({"candidate": name, "mean_relative_mae":
                        statistics.mean(ratios) if ratios and not disqualified else None})
    report["ridge_model"] = report["model"]
    report["model"] = {"name": "ensemble", "combination": "arithmetic price average",
                       "weights": {"latest_price": 0.5, "ridge": 0.5}, "fitted_weights": False}
    report["ridge_source_sha256"] = report.pop("source_sha256")
    report["source_sha256"] = sha256(Path(__file__).read_bytes())
    report["ranking"] = ranking
    report["selected"] = min((entry for entry in ranking if entry["mean_relative_mae"] is not None),
                             key=lambda entry: entry["mean_relative_mae"])["candidate"]
    report["selection_rule"] = ("Equal-weight mean of asset/horizon MAE ratios to latest price; "
                                "zero/zero groups excluded, nonzero/perfect-baseline disqualifies. "
                                "Ties favor latest price, then ensemble, then ridge.")
    report["limitations"].append("Weights fixed before scoring; ranking is exploratory, not significance.")
    return report


def main():
    parser = argparse.ArgumentParser(description="Validation-only fixed-weight forecast ensemble")
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
        parser.exit(1, f"Ensemble evaluation failed: {error}\n")
    for group in report["results"]:
        metrics = group["metrics"]
        print(f"{group['symbol']} {group['horizon_days']}d: "
              f"ensemble MAE={metrics['ensemble']['mae']:.6f}, "
              f"ridge={metrics['ridge']['mae']:.6f}, latest price={metrics['latest_price']['mae']:.6f}")
    print(f"Validation choice: {report['selected']}")


if __name__ == "__main__":
    main()
