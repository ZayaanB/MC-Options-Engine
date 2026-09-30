# Thread scaling benchmark

Measured on 2026-09-19. Results are machine-specific.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Build | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |
| Plots | Python 3.12.3, Matplotlib 3.11.2 |

`scaling.csv` prices the standard one-year ATM European call (`S=K=100`,
`r=5%`, `sigma=20%`, seed 42). After a 100K-path warm-up, each combination of
1M, 5M, or 10M paths and 1, 2, 4, or 8 threads runs three times. The CSV keeps
all runtimes and uses their median for throughput and speedup.

Threads were not pinned, and CPU frequency, thermals, and background load were
not controlled.
