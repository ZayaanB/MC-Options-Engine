# Monte Carlo hot-path profile

Measurements were collected on this development laptop on 2026-09-28. They are
specific to this source revision, compiler, standard library, machine, and
measurement protocol.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Standard library | GCC 14.3.0 libstdc++ |
| Callgrind | Valgrind 3.22.0 |

## Single-thread hot-path attribution

Linux `perf record` could not sample on this host because
`kernel.perf_event_paranoid=4`. Callgrind was therefore used on a
`RelWithDebInfo` build to collect deterministic retired-instruction counts for
a two-million-path European call:

```bash
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DBUILD_TESTING=OFF
cmake --build build-profile --target mcprice -j 4
valgrind --tool=callgrind \
  --callgrind-out-file=/tmp/mc-day18-european.callgrind \
  ./build-profile/mcprice price --type call --method mc \
  --paths 2000000 --threads 1 --seed 42
callgrind_annotate --inclusive=yes --threshold=0.5 \
  /tmp/mc-day18-european.callgrind
```

Callgrind collected 481,576,245 instructions. Attribution at the simulation
call sites was:

| Hot-path component | Instructions | Share |
| --- | ---: | ---: |
| Normal generation (`std::normal_distribution` and `std::mt19937_64`) | 223,526,642 | 46.42% |
| Terminal-price evolution (`BlackScholesModel::terminal_price`, including `exp`) | 140,000,000 | 29.07% |
| Streaming-statistics updates (`RunningStatistics::add`) | 44,000,000 | 9.14% |
| European-call payoff evaluation | 22,000,000 | 4.57% |
| Discount-factor accessor | 6,000,000 | 1.25% |

These are instruction shares, not wall-clock percentages. Inlining, library
implementation, CPU latency, and cache behavior mean instruction counts cannot
be translated directly into elapsed-time shares. They nevertheless identify
normal generation and exponential terminal evolution as the dominant work;
together they account for 75.49% of the measured instructions. Payoff dispatch
is comparatively small in this workload.

## Thread launch and useful-work crossover

`mc_thread_overhead` measures median engine runtime with the same deterministic
European-call configuration. Tiny workloads use 101 repetitions; the
one-million-path workload uses 11.

| Paths | Threads | Median runtime | Nanoseconds/path |
| ---: | ---: | ---: | ---: |
| 8 | 1 | 2.628 us | 328.500 |
| 8 | 8 | 396.367 us | 49,545.875 |
| 1,000 | 1 | 43.340 us | 43.340 |
| 1,000 | 8 | 179.755 us | 179.755 |
| 1,000,000 | 1 | 42.391 ms | 42.391 |
| 1,000,000 | 8 | 10.633 ms | 10.633 |

Thread creation and joining overwhelm useful work at 8 and 1,000 paths on this
machine. At one million paths, eight threads are approximately 3.99x faster.
The raw data are in `thread_overhead.csv`. Runs were not core-pinned and CPU
frequency, thermals, and background load were not controlled.

## Day 19 implications

The profile supports investigating normal generation and exponential
evaluation first. It does not support prioritizing payoff dispatch or replacing
streaming statistics based on this workload. Thread reuse or a minimum parallel
work threshold may help small jobs, but any change must be benchmarked against
the added complexity and the current deterministic-stream contract.
