# Final benchmark environment and methodology

Measurements were collected on this development laptop on 2026-09-29. The
runner executes each timed configuration three times in one process and reports
the median engine runtime. It performs one untimed European warm-up before the
five experiments. CPU affinity, turbo state, background load, and thermals were
not controlled, so short multithreaded measurements contain ordinary system
noise and should not be treated as universal performance claims.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) with libstdc++ |
| CMake | 3.28.3 |
| Build | `Release`, C++20 (`-O3 -DNDEBUG`) |
| Python | 3.12.3 |
| NumPy | 2.5.3 |
| Matplotlib | 3.11.2, non-interactive `Agg` backend |

## Selected measured results

| Experiment | Result on this machine |
| --- | --- |
| Convergence, 5M paths | 10.448002 estimate vs 10.450584 analytical; 0.002582 absolute error; 0.006584 standard error |
| Scaling, 5M paths | 0.203233 s at one thread; 0.045302 s and 110.370M paths/s at eight threads; 4.486x speedup |
| Antithetic, 5M paths | 2.0017x lower estimator variance than standard sampling; 0.040987 s vs 0.060265 s median runtime |
| Instrument performance, 250K paths | European: 0.010543 s at one thread and 0.002572 s at eight; 252-step Asian: 1.915794 s and 0.359650 s |
| Greeks, 1M antithetic paths | Delta 0.636479 (0.0552% relative error), Gamma 0.018786 (0.1286%), Vega/vol point 0.375724 (0.1288%) |

## Reproduction

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mc_final_benchmarks
./build/mc_final_benchmarks results/final
.venv/bin/python python/plot_final_benchmarks.py
```

Every experiment uses `S = 100`, `K = 100`, `r = 0.05`, `sigma = 0.20`,
`T = 1`, and seed 42. An identical full configuration is deterministic on this
implementation and toolchain. Changing the thread count changes worker streams,
and `std::normal_distribution` is implementation-dependent, so cross-thread or
cross-platform results are expected to be statistically consistent rather than
bit-identical.

## Experiment definitions

- **Convergence:** standard single-thread European call estimates at 1K, 10K,
  100K, 1M, and 5M trajectories, compared with analytical Black–Scholes.
- **Scaling:** a standard 5M-trajectory European call at 1, 2, 4, and 8 threads.
  Speedup uses the one-thread median; efficiency is speedup divided by threads.
- **Variance reduction:** a 5M-trajectory European call at four threads using
  standard and antithetic sampling. `num_paths` is the equal total trajectory
  budget. Antithetic pair averages are independent observations, so estimator
  variance is sample variance divided by 2.5M pair observations.
- **Instrument performance:** 250K standard trajectories for the exact-terminal
  European call and a 252-monitor arithmetic Asian call at 1, 2, 4, and 8
  threads. The Asian average excludes the initial spot and includes maturity.
  Paths per second compare user-visible workloads; simulated steps per second
  helps separate per-step throughput from the 252-fold work difference.
- **Greeks accuracy:** antithetic common-random-number Delta, Gamma, and Vega at
  100K, 500K, and 1M trajectories using four threads. Central spot bumps are 1%
  of spot and volatility bumps are 0.01 absolute. Vega is compared and reported
  per one volatility percentage point.

The Python plotting program parses every CSV with an explicit schema and checks
raw-runtime medians, throughput identities, confidence intervals, estimator
variance, antithetic observation counts, variance ratios, and analytical-error
fields before producing any plot.
