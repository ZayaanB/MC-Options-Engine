# Thread scaling benchmark environment

Measurements in `scaling.csv` were run on this development laptop on 2026-09-19.
They are not universal performance claims.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Build | `Release`, `-O3 -DNDEBUG`, C++20 |
| Plotting | Python 3.12.3, Matplotlib 3.11.2 |

Protocol: European call with spot 100, strike 100, continuously compounded rate
5%, volatility 20%, and maturity one year. Each full configuration uses seed 42.
The benchmark runs one 100,000-path warm-up, then three timed repetitions for
each path count (1M, 5M, 10M) and available thread count (1, 2, 4, 8). The median
runtime determines throughput, speedup, and efficiency. All raw runtimes remain
in the CSV. The benchmark did not pin threads, isolate CPU cores, or control
clock frequency and thermals; short-run measurements, particularly at 1M paths,
can vary substantially.
