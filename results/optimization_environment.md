# Day 19 hot-loop optimization results

Measurements were collected on this development laptop on 2026-09-28. The
before and after benchmarks used the same fixed-seed configuration and were run
in sequence without CPU pinning or thermal controls, so especially the
multithreaded result includes normal system noise.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Build | `Release`, `-O3 -DNDEBUG`, C++20 |
| Standard library | GCC 14.3.0 libstdc++ |

## Changes justified by the profile

The Day 18 profile attributed 29.07% of instructions to terminal-price
evolution and 9.14% to streaming-statistics updates. The optimization pass:

- makes terminal and stepwise GBM evolution available for compiler inlining;
- makes the per-observation Welford update available for inlining;
- caches discount factors outside simulation loops; and
- caches Asian initial price, step count, and average denominator outside its
  path loop.

The formulas, `std::mt19937_64` and `std::normal_distribution`, worker seed
derivation, and statistical definitions are unchanged.

## Before and after benchmark

`mc_hot_loop` runs a 10-million-path European call nine times per thread count
and reports the median engine runtime.

| Threads | Before throughput | After throughput | Change |
| ---: | ---: | ---: | ---: |
| 1 | 23.619M paths/s | 24.379M paths/s | +3.218% |
| 8 | 112.697M paths/s | 129.211M paths/s | +14.654% |

The single-thread result is the cleaner hot-loop comparison. A second optimized
run measured 24.501M paths/s single-threaded and 129.584M paths/s with eight
threads, consistent with the saved after result. The multithreaded improvement
should still be treated cautiously because scheduling and CPU frequency are not
controlled.

The price and standard error were bit-identical before and after for both full
configurations. Raw benchmark rows are saved in `hot_loop_before.csv` and
`hot_loop_after.csv`.

## Instruction-count check

Repeating the two-million-path Callgrind workload reduced total retired
instructions from 481,576,245 to 449,576,174, a reduction of approximately
6.645%. Normal-generation work was unchanged at about 223.5 million
instructions, so it now represents 49.72% of the smaller total. This confirms
that the optimization removed surrounding model/statistics overhead without
changing random-number generation.
