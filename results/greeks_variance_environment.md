# Common-random-number Greek benchmark

Measured on 2026-09-22. Results are machine-specific.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Build | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |

The test estimates central-difference Delta for the one-year ATM call with a
spot bump of 1. Each method runs 100 replications using two 50K-path prices per
replication. Common random numbers reuse the seed for both bumps; the comparison
uses independent seeds.

Delta variance was `6.6856e-06` with common streams and `0.0028051` with
independent streams, a 419.6x reduction. That factor depends on the option,
bump, path count, and RNG implementation.
