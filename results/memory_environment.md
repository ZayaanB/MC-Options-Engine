# Batched simulation memory measurements

Measurements were collected on this development laptop on 2026-09-27 with
GNU `/usr/bin/time -v`. They are process-level peak resident-set measurements,
not universal memory guarantees.

| Component | Measured environment |
| --- | --- |
| CPU | Intel Core i9-13900H, 14 physical cores, 20 logical threads |
| RAM | 31 GiB reported by the operating system |
| OS | Linux Mint 22.3, Linux kernel 7.0.0-31-generic |
| Compiler | GCC 14.3.0 (`/usr/bin/c++`) |
| CMake | 3.28.3 |
| Build | `Release`, `-O3 -DNDEBUG`, C++20 |
| Standard library | GCC 14.3.0 libstdc++ |

Both runs used eight worker threads, seed 42, and a batch size of 16,384
independent statistical observations per worker.

| Instrument and workload | Peak RSS | Runtime reported by engine |
| --- | ---: | ---: |
| European call, 10,000,000 standard paths | 4,124 KiB | 0.116 s |
| Arithmetic Asian call, 1,000,000 antithetic paths, 252 steps | 4,124 KiB | 1.133 s |

Commands:

```bash
/usr/bin/time -v ./build/mcprice price --type call --method mc \
  --paths 10000000 --threads 8 --batch-size 16384 --seed 42

/usr/bin/time -v ./build/mcprice price --type asian-call \
  --paths 1000000 --threads 8 --batch-size 16384 --steps 252 \
  --seed 42 --antithetic
```

The equal measured peak RSS despite very different path and time-step counts is
consistent with the implementation: it stores thread-local RNG state, running
statistics, and scalar path state, but retains neither payoff arrays nor full
paths. Peak RSS can vary with the operating system, allocator, compiler, and
measurement conditions.
