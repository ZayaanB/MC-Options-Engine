# Monte Carlo Options Pricing Engine

A portable C++20 derivatives pricing and risk engine built around reproducible,
multithreaded Monte Carlo simulation.

> **Status:** Active development. The project foundation, core domain types,
> European payoffs, analytical Black-Scholes pricing, and streaming statistics
> are implemented. Single-threaded and multithreaded Monte Carlo pricing are available through
> the engine API, validated against analytical reference values, and exercised
> through a reproducible convergence experiment.

Monte Carlo workers use independent, deterministic RNG streams and thread-local
statistics; results are merged after all workers finish. The same full configuration
reproduces an estimate on the same toolchain. Changing the thread count can change
the random streams and numerical result, while estimates remain statistically
consistent. `std::normal_distribution` does not guarantee bit-identical sequences
across standard-library implementations.
Custom instruments used with multiple threads must make `payoff()` safe for
concurrent calls; the provided European instruments are immutable.

Antithetic mode (`SimulationConfig::antithetic = true`) requires an even total
trajectory count. Each generated normal draw `Z` produces two terminal prices
from `Z` and `-Z`; their average discounted payoff is one independent statistical
observation. Thus `PricingResult::paths` remains the requested trajectory count,
while `PricingResult::observations` is half as large in antithetic mode. Its
sample variance is across pair averages, and its standard error and 95% confidence
interval use the pair count. In standard mode, paths and observations are equal.

On Linux with GCC, the parallel tests can also be checked with ThreadSanitizer:

```bash
cmake -S . -B build-tsan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS=-fsanitize=thread \
  -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-tsan --target mc_tests
./build-tsan/mc_tests --reporter compact
```

This project estimates derivative fair values under risk-neutral assumptions. It
is not a trading system, stock-price predictor, signal generator, or execution
engine.

The current model assumes zero dividends, a flat continuously compounded
risk-free rate, constant volatility, frictionless markets, and risk-neutral
geometric Brownian motion.

## Build

Requirements:

- A C++20 compiler
- CMake 3.20 or newer
- Network access during initial configuration when Catch2 is not already installed

Configure, build, and test:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Run the current executable to price the canonical European call example with one
million single-threaded paths:

```bash
./build/mcprice
```

Catch2 is discovered from the system when available. Otherwise, CMake fetches
the pinned version declared in `CMakeLists.txt` during configuration.

## Independent Python validation

The Python validator implements Black-Scholes with SciPy and Monte Carlo with
NumPy, independently of the C++ engine:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install -r python/requirements.txt
.venv/bin/python python/validate_black_scholes.py
```

It reports analytical call/put prices, Monte Carlo estimates, standard errors,
and normalized errors for the canonical scenario.

## Convergence experiment

Generate measured convergence data and plots:

```bash
./build/mc_convergence results/convergence.csv
.venv/bin/python python/plot_convergence.py
```

The experiment prices the canonical European call with 1,000 through 5,000,000
paths using a fixed seed. It records the Monte Carlo estimate, analytical price,
absolute error, standard error, and runtime. The plotting script creates:

- `results/convergence_price.png`
- `results/convergence_error.png`

## Thread scaling benchmark

Run three timed repetitions for each combination of 1M, 5M, and 10M paths with
1, 2, 4, and 8 threads, subject to reported hardware concurrency:

```bash
./build/mc_scaling results/scaling.csv
.venv/bin/python python/plot_scaling.py
```

The CSV keeps all three raw runtimes and their median. Throughput uses the median
runtime; speedup is the one-thread median divided by the corresponding thread
count's median, and parallel efficiency is speedup divided by thread count.
Results are specific to the measured development machine and are not universal
performance claims. Hardware and build details are recorded in
`results/scaling_environment.md`.

## Antithetic variance benchmark

Compare standard and antithetic European-call Monte Carlo with the same total
trajectory counts (1M and 5M), fixed seed, and 1, 2, or 4 threads when available:

```bash
./build/mc_antithetic results/antithetic.csv
```

Each configuration is timed three times. The CSV includes sample variance,
estimator variance (`standard_error²`), standard error, raw runtimes, and median
runtime. Compare **estimator variance**, not raw sample variance: one antithetic
observation averages two trajectories. Equal trajectory counts do not imply equal
normal-generator work—the antithetic version draws half as many normals. Results
are specific to the recorded development-machine environment.
The measurements and environment are in `results/antithetic.csv` and
`results/antithetic_environment.md`. For this European-call scenario, the
measured estimator variance was approximately halved at equal total paths.

## Platform targets

The engine, CLI, tests, and standard benchmarks target Linux and macOS. The
implementation requires no GPU or platform-specific numerical runtime.
