import argparse
import csv
import json
import statistics
import subprocess
from pathlib import Path

from benchmark_forecasts import run_benchmark, summarize


LOOKBACKS = (63, 126, 252)


def compare(manifest, engine, lookbacks=LOOKBACKS):
    if not lookbacks or len(set(lookbacks)) != len(lookbacks):
        raise ValueError("comparison requires distinct lookbacks")
    runs = []
    for lookback in lookbacks:
        for drift in ("zero", "historical", "shrinkage"):
            for volatility in ("sample", "ewma"):
                runs.append(run_benchmark(manifest, engine, "validation",
                            model={"drift": drift, "volatility": volatility},
                            lookback=lookback, dense=True))
    if len({(run["manifest_sha256"], run["engine_sha256"]) for run in runs}) != 1:
        raise ValueError("manifest or engine changed during comparison")
    groups = []
    for index, reference in enumerate(runs[0]["results"]):
        common = set(point["origin_date"] for point in reference["points"])
        for run in runs[1:]:
            common.intersection_update(point["origin_date"] for point in run["results"][index]["points"])
        dates = sorted(common)[::reference["horizon_days"]]
        if not dates:
            raise ValueError("no common validation origins; extend training history")
        selected = set(dates)
        metrics = []
        for run in runs:
            points = [point for point in run["results"][index]["points"]
                      if point["origin_date"] in selected]
            metrics.append(summarize(points))
        groups.append({"symbol": reference["symbol"], "horizon_days": reference["horizon_days"],
                       "origins": dates, "metrics": metrics})
    ranking = [{"candidate": "latest_price", "mean_relative_mae": 1.0}]
    for index, run in enumerate(runs):
        ratios = []
        for group in groups:
            metric = group["metrics"][index]
            baseline = metric["latest_price"]["mae"]
            error = metric["selected"]["mae"]
            if baseline == 0:
                if error != 0:
                    ratios = None
                    break
            else:
                ratios.append(error / baseline)
        ranking.append({"candidate": index, "mean_relative_mae":
                        statistics.mean(ratios) if ratios else None})
    eligible = [entry for entry in ranking if entry["mean_relative_mae"] is not None]
    winner = min(eligible, key=lambda entry: entry["mean_relative_mae"])
    return {"schema_version": 1, "phase": "validation", "manifest_sha256": runs[0]["manifest_sha256"],
            "engine_sha256": runs[0]["engine_sha256"], "config": runs[0]["config"],
            "provenance": runs[0]["provenance"], "platform": runs[0]["platform"],
            "python_version": runs[0]["python_version"],
            "candidates": [run["model"] for run in runs], "groups": groups,
            "ranking": ranking, "selected": winner["candidate"],
            "selection_rule": "Lowest equally weighted asset/horizon mean MAE/latest-price MAE; "
                              "zero/zero groups excluded; baseline wins ties, then candidate order.",
            "limitations": ["Exploratory selection, not evidence of predictive skill; no holdout scored.",
                            "Multiple comparisons can overfit validation; freeze before final evaluation.",
                            "A nonzero error against a perfect baseline makes that candidate ineligible."]}


def main():
    parser = argparse.ArgumentParser(description="Validation-only fixed GBM comparison")
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--engine", type=Path, default=Path("build/mcprice"))
    parser.add_argument("--output", type=Path, required=True)
    arguments = parser.parse_args()
    try:
        if arguments.output.exists():
            raise ValueError("output already exists; choose a new path")
        report = compare(arguments.manifest.resolve(), arguments.engine.resolve())
        encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
        with arguments.output.open("x", encoding="utf-8") as output:
            output.write(encoded)
    except (ValueError, TypeError, KeyError, OSError, csv.Error, subprocess.SubprocessError) as error:
        parser.exit(1, f"Comparison failed: {error}\n")
    print(f"Validation choice: {report['selected']}; saved to {arguments.output}")


if __name__ == "__main__":
    main()
