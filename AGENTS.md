# AI Development Guide

## Project

This repository implements a portable C++20 multithreaded Monte Carlo derivatives pricing and
risk engine. The primary objectives are quantitative correctness, systems correctness, and clear
measurement.

This is not a trading system or stock-price prediction system. Do not add execution, signals,
portfolio logic, brokerage integration, or forecasting. The engine estimates derivative fair
values and sensitivities through risk-neutral analytical and Monte Carlo methods.

## V1 Scope

V1 includes analytical Black-Scholes, European call and put Monte Carlo pricing, streaming
statistics, confidence intervals, deterministic random-number generation for an identical full
configuration, multithreading, antithetic variates, finite-difference Delta/Gamma/Vega, an
arithmetic Asian call, a CLI, tests, benchmarks, and Python analysis and plots.

Do not add control variates, barriers, Heston, Sobol sequences, CUDA, American options, or other
stretch goals unless they are explicitly requested after V1 is complete.

## Architecture

Keep instrument logic separate from simulation logic, statistics separate from presentation, and
analytical pricing separate from Monte Carlo pricing. Prefer focused types with value semantics.
Do not force terminal-only and path-dependent instruments into one oversized abstraction.

## Financial and Numerical Contract

V1 assumes zero dividends, a flat continuously compounded risk-free rate, constant volatility,
frictionless markets, and risk-neutral geometric Brownian motion.

All Monte Carlo results must report standard error and a 95% confidence interval. Validate
European results against analytical Black-Scholes values. Monte Carlo tests must use statistical
tolerances rather than require equality with analytical prices.

Fixed seeds need only be bit-reproducible on the same implementation and toolchain with an
identical full configuration, including thread count. Document that `std::normal_distribution` is
implementation-dependent. Different thread counts and platforms must produce statistically
consistent, not bit-identical, estimates.

In antithetic mode, `num_paths` counts total trajectories and must be even. Each pair's average
discounted payoff is one independent statistical observation for estimator variance and confidence
interval calculations. Reject odd path counts instead of changing them silently.

For an Asian option with M steps, monitor at jT/M for j=1,...,M. Exclude the initial spot and
include maturity. Simulate paths incrementally without storing an N-by-M matrix.

Support zero volatility and zero maturity. Require finite inputs, positive spot and strike,
nonnegative volatility and maturity, positive paths and threads, and positive steps for path
simulation. Negative interest rates are valid.

Finite-difference bumps must be configurable. Default the spot bump to 1% of spot and the absolute
volatility bump to 0.01. Report Vega per one volatility percentage point; convert a derivative with
respect to unit volatility by multiplying it by 0.01 for display.

## Performance Constraints

Target ordinary laptop CPUs on Linux and macOS. Do not require CUDA. Do not retain millions of
payoffs or complete paths. Prefer streaming calculations, mergeable running statistics, batching,
and thread-local worker state. Avoid locks in simulation hot loops. Profile before optimizing and
publish only measurements produced on the development machine with its environment documented.

## C++ Requirements

Use C++20, RAII, const correctness, `std::span` where appropriate, and value semantics where
practical. Avoid raw owning pointers, global mutable state, unnecessary inheritance, and shared
random-number generators. Keep the engine, CLI, tests, and normal benchmark executable portable
between Linux and macOS.

## CLI Contract

Use `--method mc|analytical|both`, defaulting to `both` for European calls and puts. Instruments
without an analytical implementation use Monte Carlo and clearly state that an analytical
reference is unavailable. Default European output includes the analytical price, Monte Carlo
estimate, absolute difference, standard error, confidence interval, and runtime.

## Testing and Build

Every quantitative feature requires tests. Before considering a change complete, run:

```text
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Keep commits coherent, buildable, and tested. Do not implement features outside the requested
milestone unless required for correctness. Before changing established architecture, explain why
it cannot support the requested feature.
