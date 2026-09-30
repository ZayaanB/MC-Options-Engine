# Final benchmark notes

Measured on 2026-09-29. Each timed configuration ran three times after one
warm-up; CSV summaries use the median. Threads were not pinned, and thermals,
clock speed, and background load were not controlled.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| C++ | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |
| Python | 3.12.3, NumPy 2.5.3, Matplotlib 3.11.2 |

All experiments use `S=K=100`, `r=5%`, `sigma=20%`, `T=1`, and seed 42.

| Experiment | Result |
| --- | --- |
| 5M-path convergence | 10.448002 vs 10.450584 analytical; SE 0.006584 |
| 5M-path scaling | 0.203233 s at 1 thread; 0.045302 s at 8; 4.486x speedup |
| Antithetic, 5M paths | 2.0017x lower estimator variance |
| 250K-path performance | European: 0.010543 s at 1 thread; Asian-252: 1.915794 s |
| Greeks, 1M paths | Delta, Gamma, and Vega errors: 0.0552%–0.1288% |

The five CSVs cover convergence, thread scaling, antithetic sampling, European
versus Asian performance, and Greek accuracy. Antithetic pair averages count as
independent observations. Vega is per one volatility percentage point.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target mc_final_benchmarks
./build/mc_final_benchmarks results/final
.venv/bin/python python/plot_final_benchmarks.py
```

The plotting script checks medians, throughput, confidence intervals, variance
ratios, observation counts, and analytical errors before creating plots.
Identical full configurations reproduce on this toolchain; different thread
counts or standard libraries may produce different but statistically consistent
streams.
