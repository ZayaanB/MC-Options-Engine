# Batched simulation memory

Measured on 2026-09-27 with `/usr/bin/time -v`. Results are process-level peak
RSS on this machine, not memory guarantees.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Build | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |

Both runs used eight threads, seed 42, and batches of 16,384 observations.

| Workload | Peak RSS | Runtime |
| --- | ---: | ---: |
| European call, 10M paths | 4,124 KiB | 0.116 s |
| Asian call, 1M antithetic paths, 252 steps | 4,124 KiB | 1.133 s |

```bash
/usr/bin/time -v ./build/mcprice price --type call --method mc \
  --paths 10000000 --threads 8 --batch-size 16384 --seed 42

/usr/bin/time -v ./build/mcprice price --type asian-call \
  --paths 1000000 --threads 8 --batch-size 16384 --steps 252 \
  --seed 42 --antithetic
```

The engine stores worker-local RNG, statistics, and current path state—not
payoff arrays or full paths. RSS may vary by OS, allocator, and compiler.
