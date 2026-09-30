#!/usr/bin/env python3
"""Validate the final benchmark CSVs and generate publication-ready plots."""

from __future__ import annotations

import argparse
import csv
import math
import os
import statistics
import tempfile
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "mc-options-engine-matplotlib")
)

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


REPETITIONS = 3


def read_csv(path: Path, required: set[str]) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        if reader.fieldnames is None or not required.issubset(reader.fieldnames):
            missing = required.difference(reader.fieldnames or [])
            raise ValueError(f"{path} is missing columns: {sorted(missing)}")
        rows = list(reader)
    if not rows:
        raise ValueError(f"{path} contains no data rows")
    return rows


def positive(record: dict[str, str], key: str) -> float:
    value = float(record[key])
    if not math.isfinite(value) or value <= 0.0:
        raise ValueError(f"{key} must be finite and positive, got {record[key]}")
    return value


def validate_runtimes(record: dict[str, str]) -> float:
    runtimes = [positive(record, f"runtime_{index}_seconds") for index in range(1, 4)]
    median = positive(record, "median_runtime_seconds")
    if not math.isclose(median, statistics.median(runtimes), rel_tol=1e-12):
        raise ValueError("median runtime does not match the three raw repetitions")
    return median


def load_convergence(directory: Path) -> list[dict[str, float]]:
    rows = read_csv(
        directory / "convergence.csv",
        {
            "paths", "mc_price", "analytical_price", "absolute_error", "standard_error",
            "confidence_lower", "confidence_upper", "median_runtime_seconds",
        },
    )
    parsed: list[dict[str, float]] = []
    previous_paths = 0
    analytical_reference: float | None = None
    for record in rows:
        paths = int(record["paths"])
        estimate = float(record["mc_price"])
        analytical = float(record["analytical_price"])
        error = positive(record, "absolute_error")
        standard_error = positive(record, "standard_error")
        lower = float(record["confidence_lower"])
        upper = float(record["confidence_upper"])
        runtime = validate_runtimes(record)
        if paths <= previous_paths:
            raise ValueError("convergence path counts must be positive and strictly increasing")
        if not math.isclose(error, abs(estimate - analytical), rel_tol=1e-12):
            raise ValueError("convergence absolute error is inconsistent")
        if not math.isclose(lower, estimate - 1.96 * standard_error, rel_tol=1e-12):
            raise ValueError("convergence confidence interval lower bound is inconsistent")
        if not math.isclose(upper, estimate + 1.96 * standard_error, rel_tol=1e-12):
            raise ValueError("convergence confidence interval upper bound is inconsistent")
        if analytical_reference is None:
            analytical_reference = analytical
        elif analytical != analytical_reference:
            raise ValueError("analytical reference changed within convergence experiment")
        parsed.append({"paths": paths, "estimate": estimate, "analytical": analytical,
                       "error": error, "standard_error": standard_error, "runtime": runtime})
        previous_paths = paths
    return parsed


def load_scaling(directory: Path) -> list[dict[str, float]]:
    rows = read_csv(
        directory / "scaling.csv",
        {"paths", "threads", "repetitions", "median_runtime_seconds",
         "throughput_paths_per_second", "speedup", "parallel_efficiency"},
    )
    parsed: list[dict[str, float]] = []
    baseline: float | None = None
    for record in rows:
        paths = int(record["paths"])
        threads = int(record["threads"])
        runtime = validate_runtimes(record)
        throughput = positive(record, "throughput_paths_per_second")
        speedup = positive(record, "speedup")
        efficiency = positive(record, "parallel_efficiency")
        if int(record["repetitions"]) != REPETITIONS or paths <= 0 or threads <= 0:
            raise ValueError("invalid scaling configuration")
        if threads == 1:
            baseline = runtime
        if baseline is None:
            raise ValueError("scaling data must start with the one-thread baseline")
        expected_speedup = baseline / runtime
        if not math.isclose(throughput, paths / runtime, rel_tol=1e-12):
            raise ValueError("scaling throughput is inconsistent")
        if not math.isclose(speedup, expected_speedup, rel_tol=1e-12):
            raise ValueError("scaling speedup is inconsistent")
        if not math.isclose(efficiency, speedup / threads, rel_tol=1e-12):
            raise ValueError("scaling parallel efficiency is inconsistent")
        parsed.append({"threads": threads, "runtime": runtime, "throughput": throughput,
                       "speedup": speedup, "efficiency": efficiency})
    return parsed


def load_variance(directory: Path) -> list[dict[str, float | str]]:
    rows = read_csv(
        directory / "variance_reduction.csv",
        {"mode", "paths", "observations", "sample_variance", "estimator_variance",
         "standard_error", "variance_reduction_vs_standard", "median_runtime_seconds"},
    )
    if [record["mode"] for record in rows] != ["standard", "antithetic"]:
        raise ValueError("variance data must contain standard then antithetic rows")
    standard_variance = positive(rows[0], "estimator_variance")
    parsed: list[dict[str, float | str]] = []
    for record in rows:
        mode = record["mode"]
        paths = int(record["paths"])
        observations = int(record["observations"])
        sample_variance = positive(record, "sample_variance")
        estimator_variance = positive(record, "estimator_variance")
        standard_error = positive(record, "standard_error")
        ratio = positive(record, "variance_reduction_vs_standard")
        runtime = validate_runtimes(record)
        expected_observations = paths if mode == "standard" else paths // 2
        if observations != expected_observations:
            raise ValueError(f"incorrect observation count for {mode}")
        if not math.isclose(estimator_variance, sample_variance / observations, rel_tol=1e-12):
            raise ValueError(f"estimator variance is inconsistent for {mode}")
        if not math.isclose(standard_error**2, estimator_variance, rel_tol=1e-12):
            raise ValueError(f"standard error is inconsistent for {mode}")
        if not math.isclose(ratio, standard_variance / estimator_variance, rel_tol=1e-12):
            raise ValueError(f"variance-reduction ratio is inconsistent for {mode}")
        parsed.append({"mode": mode, "variance": estimator_variance,
                       "runtime": runtime, "ratio": ratio})
    return parsed


def load_instruments(directory: Path) -> list[dict[str, float | str]]:
    rows = read_csv(
        directory / "instrument_performance.csv",
        {"instrument", "paths", "threads", "steps", "median_runtime_seconds",
         "throughput_paths_per_second", "throughput_steps_per_second"},
    )
    parsed: list[dict[str, float | str]] = []
    seen: set[tuple[str, int]] = set()
    for record in rows:
        instrument = record["instrument"]
        paths = int(record["paths"])
        threads = int(record["threads"])
        steps = int(record["steps"])
        runtime = validate_runtimes(record)
        path_throughput = positive(record, "throughput_paths_per_second")
        step_throughput = positive(record, "throughput_steps_per_second")
        key = (instrument, threads)
        if instrument not in {"european_call", "asian_call"} or key in seen:
            raise ValueError(f"invalid or duplicate instrument row: {key}")
        if paths <= 0 or threads <= 0 or steps <= 0:
            raise ValueError(f"invalid instrument configuration: {key}")
        if not math.isclose(path_throughput, paths / runtime, rel_tol=1e-12):
            raise ValueError(f"path throughput is inconsistent for {key}")
        if not math.isclose(step_throughput, path_throughput * steps, rel_tol=1e-12):
            raise ValueError(f"step throughput is inconsistent for {key}")
        parsed.append({"instrument": instrument, "threads": threads, "runtime": runtime,
                       "path_throughput": path_throughput, "step_throughput": step_throughput})
        seen.add(key)
    return parsed


def load_greeks(directory: Path) -> list[dict[str, float | str]]:
    rows = read_csv(
        directory / "greeks_accuracy.csv",
        {"paths", "greek", "mc_estimate", "analytical_value", "absolute_error",
         "relative_error", "median_runtime_seconds"},
    )
    parsed: list[dict[str, float | str]] = []
    seen: set[tuple[int, str]] = set()
    references: dict[str, float] = {}
    for record in rows:
        paths = int(record["paths"])
        greek = record["greek"]
        estimate = float(record["mc_estimate"])
        analytical = float(record["analytical_value"])
        error = positive(record, "absolute_error")
        relative_error = positive(record, "relative_error")
        runtime = validate_runtimes(record)
        key = (paths, greek)
        if paths <= 0 or key in seen:
            raise ValueError(f"invalid or duplicate Greek row: {key}")
        if not math.isclose(error, abs(estimate - analytical), rel_tol=1e-12):
            raise ValueError(f"absolute Greek error is inconsistent for {key}")
        if not math.isclose(relative_error, error / abs(analytical), rel_tol=1e-12):
            raise ValueError(f"relative Greek error is inconsistent for {key}")
        if greek in references and analytical != references[greek]:
            raise ValueError(f"analytical value changed for {greek}")
        references[greek] = analytical
        parsed.append({"paths": paths, "greek": greek, "estimate": estimate,
                       "analytical": analytical, "error": error,
                       "relative_error": relative_error, "runtime": runtime})
        seen.add(key)
    return parsed


def save(figure: plt.Figure, path: Path) -> None:
    figure.tight_layout()
    figure.savefig(path, dpi=180)
    plt.close(figure)
    print(f"Wrote {path}")


def plot_convergence(rows: list[dict[str, float]], path: Path) -> None:
    paths = [row["paths"] for row in rows]
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    axes[0].semilogx(paths, [row["estimate"] for row in rows], marker="o", label="Monte Carlo")
    axes[0].axhline(rows[0]["analytical"], color="black", linestyle="--", label="Black–Scholes")
    axes[0].set(xlabel="Trajectories", ylabel="Call price", title="Price convergence")
    axes[0].legend()
    axes[1].loglog(paths, [row["error"] for row in rows], marker="o", label="Absolute error")
    axes[1].loglog(paths, [row["standard_error"] for row in rows], marker="s", label="Standard error")
    axes[1].set(xlabel="Trajectories", ylabel="Error", title="Sampling error")
    axes[1].legend()
    for axis in axes:
        axis.grid(True, which="both", alpha=0.25)
    save(figure, path)


def plot_scaling(rows: list[dict[str, float]], path: Path) -> None:
    threads = [row["threads"] for row in rows]
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    axes[0].plot(threads, [row["runtime"] for row in rows], marker="o")
    axes[0].set(xlabel="CPU threads", ylabel="Median runtime (s)", title="5M-path runtime")
    axes[1].plot(threads, [row["speedup"] for row in rows], marker="o", label="Measured")
    axes[1].plot(threads, threads, linestyle="--", color="black", label="Ideal")
    axes[1].set(xlabel="CPU threads", ylabel="Speedup vs one thread", title="Parallel scaling")
    axes[1].legend()
    for axis in axes:
        axis.set_xticks(threads)
        axis.grid(True, alpha=0.25)
    save(figure, path)


def plot_variance(rows: list[dict[str, float | str]], path: Path) -> None:
    labels = [str(row["mode"]).title() for row in rows]
    figure, axes = plt.subplots(1, 2, figsize=(10, 4.8))
    axes[0].bar(labels, [float(row["variance"]) for row in rows], color=["#4C78A8", "#F58518"])
    axes[0].set(ylabel="Estimator variance", title="Equal-trajectory uncertainty")
    axes[1].bar(labels, [float(row["runtime"]) for row in rows], color=["#4C78A8", "#F58518"])
    axes[1].set(ylabel="Median runtime (s)", title="Equal-trajectory runtime")
    for axis in axes:
        axis.grid(True, axis="y", alpha=0.25)
    save(figure, path)


def plot_instruments(rows: list[dict[str, float | str]], path: Path) -> None:
    figure, axes = plt.subplots(1, 2, figsize=(12, 4.8))
    labels = {"european_call": "European call (1 step)", "asian_call": "Asian call (252 steps)"}
    for instrument in ("european_call", "asian_call"):
        subset = [row for row in rows if row["instrument"] == instrument]
        axes[0].plot([row["threads"] for row in subset], [row["runtime"] for row in subset],
                     marker="o", label=labels[instrument])
        axes[1].plot([row["threads"] for row in subset],
                     [float(row["step_throughput"]) / 1_000_000 for row in subset],
                     marker="o", label=labels[instrument])
    axes[0].set_yscale("log")
    axes[0].set(xlabel="CPU threads", ylabel="Median runtime (s)", title="250K-trajectory runtime")
    axes[1].set(xlabel="CPU threads", ylabel="Million simulated steps/s", title="Simulation throughput")
    for axis in axes:
        axis.grid(True, which="both", alpha=0.25)
        axis.legend()
    save(figure, path)


def plot_greeks(rows: list[dict[str, float | str]], path: Path) -> None:
    figure, axis = plt.subplots(figsize=(8, 5))
    labels = {"delta": "Delta", "gamma": "Gamma", "vega_per_volatility_point": "Vega / 1 vol pt"}
    for greek, label in labels.items():
        subset = [row for row in rows if row["greek"] == greek]
        axis.loglog([row["paths"] for row in subset],
                    [100.0 * float(row["relative_error"]) for row in subset],
                    marker="o", label=label)
    axis.set(xlabel="Trajectories", ylabel="Relative error (%)",
             title="Finite-difference Greeks vs Black–Scholes")
    axis.grid(True, which="both", alpha=0.25)
    axis.legend()
    save(figure, path)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input-dir", type=Path, default=Path("results/final"))
    parser.add_argument("--output-dir", type=Path, default=Path("results/final"))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)

    convergence = load_convergence(args.input_dir)
    scaling = load_scaling(args.input_dir)
    variance = load_variance(args.input_dir)
    instruments = load_instruments(args.input_dir)
    greeks = load_greeks(args.input_dir)

    plot_convergence(convergence, args.output_dir / "convergence.png")
    plot_scaling(scaling, args.output_dir / "scaling.png")
    plot_variance(variance, args.output_dir / "variance_reduction.png")
    plot_instruments(instruments, args.output_dir / "instrument_performance.png")
    plot_greeks(greeks, args.output_dir / "greeks_accuracy.png")
    print("Validated all five final benchmark datasets.")


if __name__ == "__main__":
    main()
