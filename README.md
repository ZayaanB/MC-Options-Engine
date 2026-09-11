# Monte Carlo Options Pricing Engine

A portable C++20 derivatives pricing and risk engine built around reproducible,
multithreaded Monte Carlo simulation.

> **Status:** Active development. The project foundation, core domain types, and
> European call/put payoff abstractions are implemented.

This project estimates derivative fair values under risk-neutral assumptions. It
is not a trading system, stock-price predictor, signal generator, or execution
engine.

## Planned V1 scope

- Analytical Black-Scholes pricing for European calls and puts
- Single-threaded and multithreaded Monte Carlo pricing
- Streaming statistics, standard errors, and 95% confidence intervals
- Deterministic random-number generation for a fixed full configuration
- Antithetic variates
- Finite-difference Delta, Gamma, and Vega using common random numbers
- Memory-efficient arithmetic Asian call pricing
- Reproducible convergence, scaling, and performance benchmarks
- Python validation and visualization scripts

V1 assumes zero dividends, a flat continuously compounded risk-free rate,
constant volatility, frictionless markets, and risk-neutral geometric Brownian
motion.

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

Run the current executable:

```bash
./build/mcprice
```

Catch2 is discovered from the system when available. Otherwise, CMake fetches
the pinned version declared in `CMakeLists.txt` during configuration.

## Platform targets

The engine, CLI, tests, and standard benchmarks target Linux and macOS. The
implementation requires no GPU or platform-specific numerical runtime.

## License

No license has been selected yet. Until one is added, all rights are reserved.
