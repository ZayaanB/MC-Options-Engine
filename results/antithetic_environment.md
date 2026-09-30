# Antithetic benchmark

Measured on 2026-09-20. Results are machine-specific.

| Environment | Value |
| --- | --- |
| CPU / RAM | Intel Core i9-13900H, 14 cores / 20 threads; 31 GiB |
| OS | Linux Mint 22.3, kernel 7.0.0-31-generic |
| Build | GCC 14.3.0, CMake 3.28.3, C++20 Release (`-O3 -DNDEBUG`) |

`antithetic.csv` compares standard and antithetic pricing for the one-year ATM
European call (`S=K=100`, `r=5%`, `sigma=20%`, seed 42). Both methods use the
same total trajectory counts, 1M or 5M, with 1, 2, or 4 threads. Each full
configuration runs three times after a 100K-path warm-up.

Compare estimator variance (`standard_error²`), not raw sample variance. An
antithetic pair is one observation and uses one normal draw. In this test it cut
estimator variance by about 2x; that result is specific to this payoff and setup.
