#!/usr/bin/env python3
"""Validate measured scaling data and plot runtime, speedup, and throughput."""

from __future__ import annotations

import argparse
import csv
import math
import os
import statistics
import tempfile
from dataclasses import dataclass
from pathlib import Path

os.environ.setdefault(
    "MPLCONFIGDIR", str(Path(tempfile.gettempdir()) / "mc-options-engine-matplotlib")
)

import matplotlib

matplotlib.use("Agg")

import matplotlib.pyplot as plt


@dataclass(frozen=True)
class ScalingRow:
    paths: int
    threads: int
    runtime: float
    throughput: float
    speedup: float
    efficiency: float


def read_scaling(path: Path) -> list[ScalingRow]:
    with path.open(newline="", encoding="utf-8") as source:
        raw_rows = list(csv.DictReader(source))
    if not raw_rows:
        raise ValueError(f"no scaling rows found in {path}")

    rows: list[ScalingRow] = []
    seen: set[tuple[int, int]] = set()
    baseline: dict[int, float] = {}

    for record in raw_rows:
        paths = int(record["paths"])
        threads = int(record["threads"])
        repetitions = int(record["repetitions"])
        runtimes = [float(record[f"runtime_{index}_seconds"]) for index in range(1, 4)]
        runtime = float(record["median_runtime_seconds"])
        throughput = float(record["throughput_paths_per_second"])
        speedup = float(record["speedup"])
        efficiency = float(record["parallel_efficiency"])
        key = (paths, threads)

        if paths <= 0 or threads <= 0 or repetitions != 3 or key in seen:
            raise ValueError(f"invalid or duplicate scaling row: {key}")
        if any(not math.isfinite(value) or value <= 0 for value in runtimes):
            raise ValueError(f"invalid runtime measurements for {key}")
        if not math.isclose(runtime, statistics.median(runtimes), rel_tol=1e-12):
            raise ValueError(f"median runtime does not match raw runs for {key}")
        if not math.isclose(throughput, paths / runtime, rel_tol=1e-12):
            raise ValueError(f"throughput does not match paths/runtime for {key}")
        if threads == 1:
            baseline[paths] = runtime
        seen.add(key)
        rows.append(ScalingRow(paths, threads, runtime, throughput, speedup, efficiency))

    for row in rows:
        if row.paths not in baseline:
            raise ValueError(f"missing one-thread baseline for {row.paths} paths")
        expected_speedup = baseline[row.paths] / row.runtime
        if not math.isclose(row.speedup, expected_speedup, rel_tol=1e-12):
            raise ValueError(f"speedup does not match baseline/runtime for {row.paths} paths")
        if not math.isclose(row.efficiency, expected_speedup / row.threads, rel_tol=1e-12):
            raise ValueError(f"parallel efficiency is inconsistent for {row.paths} paths")

    return rows


def plot_metric(rows: list[ScalingRow], metric: str, label: str, title: str, path: Path) -> None:
    figure, axis = plt.subplots(figsize=(8, 5))
    for paths in sorted({row.paths for row in rows}):
        subset = sorted((row for row in rows if row.paths == paths), key=lambda row: row.threads)
        axis.plot(
            [row.threads for row in subset],
            [getattr(row, metric) / (1_000_000 if metric == "throughput" else 1)
             for row in subset],
            marker="o",
            label=f"{paths:,} paths",
        )

    thread_counts = sorted({row.threads for row in rows})
    if metric == "speedup":
        axis.plot(thread_counts, thread_counts, linestyle="--", color="black", label="Ideal")
    axis.set_xticks(thread_counts)
    axis.set_xlabel("CPU threads")
    axis.set_ylabel(label)
    axis.set_title(title)
    axis.grid(True, alpha=0.25)
    axis.legend()
    figure.tight_layout()
    figure.savefig(path, dpi=180)
    plt.close(figure)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=Path("results/scaling.csv"))
    parser.add_argument("--output-dir", type=Path, default=Path("results"))
    args = parser.parse_args()

    rows = read_scaling(args.input)
    args.output_dir.mkdir(parents=True, exist_ok=True)
    plots = (
        ("runtime", "Median runtime (s)", "Monte Carlo runtime by thread count", "scaling_runtime.png"),
        ("speedup", "Speedup vs 1 thread", "Monte Carlo parallel speedup", "scaling_speedup.png"),
        (
            "throughput",
            "Throughput (million paths/s)",
            "Monte Carlo throughput by thread count",
            "scaling_throughput.png",
        ),
    )
    for metric, label, title, filename in plots:
        output_path = args.output_dir / filename
        plot_metric(rows, metric, label, title, output_path)
        print(f"Wrote {output_path}")


if __name__ == "__main__":
    main()
