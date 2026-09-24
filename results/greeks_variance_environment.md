# Common-random-number Greeks benchmark environment

Measurements in `greeks_variance.csv` were run on this development laptop on
2026-09-22. These measurements are not universal performance claims.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Build | `Release`, `-O3 -DNDEBUG`, C++20 |
| Standard library | GCC 14.3.0 libstdc++ |

Protocol: central-difference Delta for a European call with spot 100, strike
100, continuously compounded rate 5%, volatility 20%, maturity one year, and
an absolute spot bump of 1. Each method runs 100 replications. Every replication
uses two 50,000-path single-threaded prices, so both methods have the same total
trajectory budget. Replication seeds begin at 42. The common method gives the up
and down valuations the same seed; the independent method offsets the down seed.

The measured sample variance across Delta estimates was
`6.6856155900127665e-06` with common random numbers and
`0.0028051238670599929` with independent random numbers, a variance ratio of
approximately 419.6. This magnitude is specific to this option, bump, path
count, pseudorandom generator implementation, and machine experiment; it is not
a universal reduction factor.
