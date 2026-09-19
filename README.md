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

## Platform targets

The engine, CLI, tests, and standard benchmarks target Linux and macOS. The
implementation requires no GPU or platform-specific numerical runtime.
