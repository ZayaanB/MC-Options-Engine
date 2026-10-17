import argparse
import csv
import json
import platform
import statistics
from pathlib import Path

from benchmark_forecasts import HORIZONS, read_asset, read_manifest, sha256, unique_object
from residual_forecasts import evaluate


SOURCES = ("residual_forecasts.py", "ridge_forecasts.py", "ensemble_forecasts.py",
           "benchmark_forecasts.py", "final_forecast.py")


def source_hashes():
    return {name: sha256(Path(__file__).with_name(name).read_bytes()) for name in SOURCES}


def check_inputs(manifest):
    config, digest = read_manifest(manifest)
    for asset in config["assets"]:
        read_asset(asset, manifest.parent)
    return config, digest


def freeze(manifest, validation, model):
    if model not in {"ensemble", "latest_price"}:
        raise ValueError("choose ensemble or latest_price before the final test")
    config, digest = check_inputs(manifest)
    raw = validation.read_bytes()
    report = json.loads(raw, object_pairs_hook=unique_object)
    hashes = source_hashes()
    expected = {name: value for name, value in hashes.items() if name != "final_forecast.py"}
    if (not isinstance(report, dict) or type(report.get("schema_version")) is not int or
            report.get("schema_version") != 1 or report.get("phase") != "validation" or
            report.get("manifest_sha256") != digest or report.get("config") != config or
            report.get("python_version") != platform.python_version() or
            report.get("platform") != platform.platform() or
            report.get("source_hashes") != expected or not report.get("results")):
        raise ValueError("require a current residual validation report matching inputs and source hashes")
    groups = report["results"]
    expected_groups = {(asset["symbol"], horizon) for asset in config["assets"] for horizon in HORIZONS}
    seen = set()
    if not isinstance(groups, list):
        raise ValueError("validation report requires complete scored groups")
    for group in groups:
        if not isinstance(group, dict):
            raise ValueError("invalid validation group")
        key = (group.get("symbol"), group.get("horizon_days"))
        points = group.get("points")
        if key not in expected_groups or key in seen or not isinstance(points, list) or not points:
            raise ValueError("validation report requires distinct complete scored groups")
        if group.get("forecasts") != len(points):
            raise ValueError("validation forecast count does not match points")
        previous_origin = ""
        for point in points:
            if (not isinstance(point, dict) or not isinstance(point.get("origin_date"), str) or
                    not isinstance(point.get("target_date"), str) or not
                    config["validation_start"] <= point["origin_date"] < point["target_date"] < config["holdout_start"]):
                raise ValueError("validation points cross the frozen split")
            if point["origin_date"] <= previous_origin:
                raise ValueError("validation origins must be distinct and increasing")
            previous_origin = point["origin_date"]
        seen.add(key)
    if seen != expected_groups:
        raise ValueError("validation report is missing asset/horizon groups")
    return {"schema_version": 1, "manifest_sha256": digest, "source_hashes": hashes,
            "validation_sha256": sha256(raw), "primary_model": model,
            "python_version": platform.python_version(), "platform": platform.platform(), "config": config,
            "evaluation": "Sequential rolling refits; earlier observed holdout outcomes may enter later fits.",
            "primary_metric": "Equal-weight mean of asset/horizon MAE ratios to latest price; "
                              "exclude zero/zero groups; nonzero/perfect-baseline is unavailable.",
            "limitations": "Local guard only: cannot prove the holdout was previously untouched."}


def verify(procedure, manifest):
    required = {"schema_version", "manifest_sha256", "source_hashes", "validation_sha256",
                "primary_model", "python_version", "platform", "config", "evaluation", "primary_metric", "limitations"}
    if not isinstance(procedure, dict) or set(procedure) != required:
        raise ValueError("invalid frozen procedure fields")
    if type(procedure["schema_version"]) is not int or procedure["schema_version"] != 1:
        raise ValueError("unsupported procedure version")
    if procedure["primary_model"] not in {"ensemble", "latest_price"}:
        raise ValueError("unsupported frozen primary model")
    config, digest = check_inputs(manifest)
    if procedure["manifest_sha256"] != digest or procedure["config"] != config:
        raise ValueError("manifest changed after freezing")
    if procedure["source_hashes"] != source_hashes():
        raise ValueError("source changed after freezing")
    if procedure["python_version"] != platform.python_version():
        raise ValueError("Python version changed after freezing")
    if procedure["platform"] != platform.platform():
        raise ValueError("platform changed after freezing")


def save_new(path, report):
    encoded = json.dumps(report, indent=2, sort_keys=True, allow_nan=False) + "\n"
    with path.open("x", encoding="utf-8") as stream:
        stream.write(encoded)


def run_final(procedure_path, manifest, output, confirmed=False):
    if not confirmed:
        raise ValueError("explicit --confirm-untouched-holdout is required")
    if output.exists():
        raise ValueError("output already exists; preserve the prior result")
    marker = procedure_path.with_name(procedure_path.name + ".used")
    if marker.exists():
        raise ValueError("holdout attempt already recorded for this procedure")
    if output.resolve() == marker.resolve() or not output.parent.is_dir():
        raise ValueError("choose an existing output directory and a path distinct from the attempt marker")
    raw = procedure_path.read_bytes()
    procedure = json.loads(raw, object_pairs_hook=unique_object)
    verify(procedure, manifest)
    save_new(marker, {"procedure_sha256": sha256(raw), "status": "holdout_started"})
    report = evaluate(manifest, phase="holdout")
    verify(procedure, manifest)
    if procedure_path.read_bytes() != raw:
        raise ValueError("procedure changed during evaluation")
    report["procedure_sha256"] = sha256(raw)
    report["validation_sha256"] = procedure["validation_sha256"]
    report["primary_model"] = procedure["primary_model"]
    report["final_workflow_source_sha256"] = procedure["source_hashes"]["final_forecast.py"]
    ratios = []
    unavailable = False
    for group in report["results"]:
        metrics = group["metrics"]
        baseline = metrics["latest_price"]["point"]["mae"]
        error = metrics[procedure["primary_model"]]["point"]["mae"]
        if baseline:
            ratios.append(error / baseline)
        elif error:
            unavailable = True
    report["primary_mean_relative_mae"] = statistics.mean(ratios) if ratios and not unavailable else None
    report["limitations"].append("One local attempt recorded; moving/deleting the marker can bypass this guard.")
    save_new(output, report)
    return report


def main():
    parser = argparse.ArgumentParser(description="Freeze and explicitly evaluate a final forecast procedure")
    commands = parser.add_subparsers(dest="command", required=True)
    frozen = commands.add_parser("freeze")
    frozen.add_argument("--manifest", type=Path, required=True)
    frozen.add_argument("--validation", type=Path, required=True)
    frozen.add_argument("--model", choices=("ensemble", "latest_price"), required=True)
    frozen.add_argument("--output", type=Path, required=True)
    run = commands.add_parser("run")
    run.add_argument("--manifest", type=Path, required=True)
    run.add_argument("--procedure", type=Path, required=True)
    run.add_argument("--output", type=Path, required=True)
    run.add_argument("--confirm-untouched-holdout", action="store_true")
    arguments = parser.parse_args()
    try:
        if arguments.command == "freeze":
            save_new(arguments.output, freeze(arguments.manifest.resolve(), arguments.validation, arguments.model))
            print(f"Frozen {arguments.model}; no holdout scored. Saved {arguments.output}")
        else:
            report = run_final(arguments.procedure.resolve(), arguments.manifest.resolve(), arguments.output,
                               arguments.confirm_untouched_holdout)
            print(f"Final {report['primary_model']} mean relative MAE: {report['primary_mean_relative_mae']}")
    except (ValueError, TypeError, KeyError, OSError, csv.Error, OverflowError) as error:
        parser.exit(1, f"Final workflow failed: {error}\n")


if __name__ == "__main__":
    main()
