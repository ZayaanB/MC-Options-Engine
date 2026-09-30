# Monte Carlo hot-path profile

Measured on 2026-09-28. `perf` was unavailable because
`kernel.perf_event_paranoid=4`, so the profile used Callgrind.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Tools | GCC 14.3.0, CMake 3.28.3, libstdc++, Valgrind 3.22.0 |

A two-million-path, single-thread European call retired 481,576,245
instructions:

| Component | Share |
| --- | ---: |
| Normal generation | 46.42% |
| Terminal evolution, including `exp` | 29.07% |
| Welford update | 9.14% |
| Call payoff | 4.57% |
| Discount accessor | 1.25% |

These are instruction shares, not elapsed-time shares.

`mc_thread_overhead` also measured the cost of creating workers:

| Paths | 1 thread | 8 threads |
| ---: | ---: | ---: |
| 8 | 2.628 us | 396.367 us |
| 1,000 | 43.340 us | 179.755 us |
| 1,000,000 | 42.391 ms | 10.633 ms |

Threads lose on tiny jobs but were 3.99x faster at one million paths. Runs were
not pinned and system load was not isolated. Raw data are in
`thread_overhead.csv`.

```bash
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=OFF
cmake --build build-profile --target mcprice -j 4
valgrind --tool=callgrind ./build-profile/mcprice price \
  --type call --method mc --paths 2000000 --threads 1 --seed 42
```
