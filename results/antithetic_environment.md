# Antithetic benchmark environment

Measurements in `antithetic.csv` were run on this development laptop on
2026-09-20. These measurements are not universal performance claims.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Build | `Release`, `-O3 -DNDEBUG`, C++20 |
| Standard library | GCC 14.3.0 libstdc++ |

Protocol: European call with spot 100, strike 100, continuously compounded rate
5%, volatility 20%, and maturity one year. Standard and antithetic runs each
price 1M or 5M total trajectories using seed 42 and 1, 2, or 4 threads. Both
code paths receive a 100,000-trajectory warm-up. Each full configuration runs
three times; the CSV preserves all runtimes and their median. No CPU pinning,
thermal control, or isolated load was used, so runtime comparisons are noisy.

The fair comparison of estimation uncertainty is `estimator_variance` (the
square of the reported standard error). Antithetic mode forms one observation
from each pair and therefore has half as many observations at the same total
trajectory count. It also uses half as many normal draws. Here, its estimator
variance was approximately 2x lower across measured configurations. This is a
property of this call scenario, not a guarantee for other payoffs or inputs.
