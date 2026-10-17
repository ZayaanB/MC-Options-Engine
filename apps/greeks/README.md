# Greek calculator

Calculates European call/put Delta, Gamma and Vega with finite differences and
common random numbers. Uses the same C++ engine as pricing. Build with the
[root instructions](../../README.md); run commands from the repository root.

```bash
./build/mcprice greeks --type call \
  --spot 100 --strike 100 --rate 0.05 --volatility 0.20 --maturity 1 \
  --paths 1000000 --threads 4 --seed 42 --antithetic

./build/mcprice price --type put --greeks \
  --paths 1000000 --antithetic --spot-bump 0.5 --vol-bump 0.005
```

| Output | Meaning |
| --- | --- |
| Delta | Option-price change per unit change in spot |
| Gamma | Change in Delta per unit change in spot |
| Vega | Option-price change per one volatility percentage point |

Default bumps are 1% of spot and 0.01 absolute volatility. `--spot-bump` is an
absolute price amount; `--vol-bump` is absolute volatility, not percent text.
Vega uses a forward difference when the lower bump would make volatility
negative. Each calculation runs five MC valuations; `--paths` applies to each
valuation, not the sum of all five.

`greeks` always uses Monte Carlo, so it does not accept `--method`.
`price --greeks` works with `--method mc|both`, not analytical-only pricing.
Invalid bumps and Asian Greeks are rejected. Theta, Rho and Greek confidence
intervals are not implemented. Bump error and simulation noise both matter.
At an expiry payoff kink, finite differences are not smooth analytical Greeks.
These sensitivities do not predict the stock's direction.

Code: [GreeksEngine](../../src/pricing/greeks_engine.cpp).
Tests compare call/put Greeks with analytical Black-Scholes values:

```bash
./build/mc_tests "[greeks]"
ctest --test-dir build -R greeks_cli_workflow --output-on-failure
```
