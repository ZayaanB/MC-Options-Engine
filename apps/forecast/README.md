# Stock forecast

Fits historical GBM to daily prices and reports a mean, median, model range and
probability of finishing above the latest price. This is separate from
risk-neutral option pricing. It reads files, not live ticker quotes.

Build using the [root instructions](../../README.md). From the repository root:

```bash
./build/mcprice forecast --csv examples/sample_prices.csv \
  --metadata examples/sample_prices.meta --lookback-days 10 --horizon-days 20

./build/mcprice backtest --csv examples/sample_prices.csv \
  --lookback-days 10 --horizon-days 3 --step-days 3
```

The bundled data is synthetic, not an Apple accuracy test. For real stocks,
provide chronological `Date,Adj Close` data and optional provenance metadata.
Use more history and an appropriate lookback, such as 252 daily returns.
Horizon days count trading sessions after the last supplied row, even if stale.

Backtests compare errors with latest-price and other baselines, plus direction,
probability calibration and range coverage. No reliable predictive edge has
been established in the current Apple diagnostic. A model range is not a
guarantee and excludes news, jumps and parameter uncertainty.

See the [data and run walkthrough](../../docs/forecast-workflow.md) and
[multi-stock benchmark](../../docs/forecast-benchmark.md). Feature-based models
have their own [prediction guide](../prediction/README.md).

Code: [C++ forecasting](../../src/forecasting/).

```bash
ctest --test-dir build -R 'history_cli_workflow|forecast_benchmark_runner' --output-on-failure
```
