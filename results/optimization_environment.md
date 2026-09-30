# Hot-loop optimization

Measured on 2026-09-28 without CPU pinning or thermal control.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Build | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |

Profiling pointed to terminal evolution and Welford updates. The change exposed
small model/statistics methods to inlining and cached discount factors and path
constants outside the hot loops. RNG streams, formulas, and statistical
definitions did not change.

| Threads | Before | After | Change |
| ---: | ---: | ---: | ---: |
| 1 | 23.619M paths/s | 24.379M paths/s | +3.218% |
| 8 | 112.697M paths/s | 129.211M paths/s | +14.654% |

Prices and standard errors remained bit-identical. Callgrind instructions fell
from 481,576,245 to 449,576,174 (-6.645%). Raw rows are in
`hot_loop_before.csv` and `hot_loop_after.csv`. The single-thread result is the
cleaner comparison; multithreaded timing is noisier.
