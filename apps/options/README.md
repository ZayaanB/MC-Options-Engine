# Options engine

Prices European calls/puts with analytical Black-Scholes and Monte Carlo.
Arithmetic Asian calls use a separate path simulation engine. Build once using
the [root instructions](../../README.md); run commands from the repository root.

```bash
./build/mcprice price --type call \
  --spot 100 --strike 100 --rate 0.05 --volatility 0.20 --maturity 1 \
  --paths 1000000 --threads 4 --seed 42 --antithetic --greeks

./build/mcprice price --type asian-call --steps 252 \
  --paths 100000 --threads 4 --antithetic
```

European pricing defaults to `--method both`; `mc` and `analytical` also work.
Asian pricing always uses Monte Carlo and has no analytical reference or Greeks.
`--paths` counts total trajectories, including both members of antithetic pairs.
Output includes price, standard error, 95% sampling interval and runtime. With
fewer than two independent observations, sampling uncertainty is unavailable.

Inputs are model assumptions, not fetched market quotes. Rates and volatility
are decimals; maturity is years. The model assumes zero dividends and constant
volatility. Sampling intervals do not cover model error.

Code: [pricing](../../src/pricing/), [instruments](../../src/instruments/).
Details: [mathematics](../../docs/mathematics.md).

```bash
./build/mc_tests "[black-scholes]"
./build/mc_tests "[monte-carlo]"
./build/mc_tests "[asian]"
```
