# Run a stock forecast yourself

Run commands from the repository root. The CLI reads a local history file; it
does not fetch tickers or live quotes. Forecasting currently uses historical
GBM. Historical-bootstrap forecasts have not been implemented yet.

## 1. Build and check the installation

Use CMake 3.24+ for the current dependency-download path (the project declares
3.20, but uses a download option added in 3.24). Install a C++20 compiler with support for
`std::jthread`, calendar dates, and floating-point `std::from_chars`.
The first configure needs internet access to download Catch2 unless installed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel 4
ctest --test-dir build --output-on-failure
./build/mcprice --help
```

Try the bundled synthetic history before using real data:

```bash
./build/mcprice forecast --csv examples/sample_prices.csv --horizon-days 20
./build/mcprice backtest --csv examples/sample_prices.csv \
  --lookback-days 10 --horizon-days 3 --step-days 3
```

## 2. Prepare daily stock history

Export daily historical prices from a data provider you have access to. For
Apple, save the export as `Notes-dont-commit/AAPL.csv`. That directory is
ignored by Git. Use the same workflow for any other ticker.

The file must have one header row and one row per trading session, oldest first:

```csv
Date,Adj Close
2024-01-02,100.00
2024-01-03,101.00
2024-01-04,100.50
```

These numbers only illustrate the format. Use actual exported prices for a
stock experiment. Extra columns are allowed; each row must match the header's
column count. Dates must be unique, valid `YYYY-MM-DD` dates. Prices must be
positive finite numbers without currency symbols or thousands separators.

Prefer prices consistently adjusted for splits and dividends. Do not rename an
unadjusted column to imply adjustment. A `Close` column works with
`--price-column Close`, but corporate actions can distort its returns. Adjusted
price forecasts describe the provider's adjusted-price series, which may differ
from future quoted dollar prices after corporate actions.

Use daily sessions with no accidental missing rows: the engine counts rows as
trading days and does not check exchange holidays or detect data gaps. Monthly
or intraday history does not match the daily horizon convention.

A forecast needs at least three prices. A backtest needs at least
`lookback + horizon + 1` prices; that minimum gives only one forecast. Use much
more history for evaluation. Record the provider, retrieval date, adjustment
convention, and file checksum with your results:

```bash
sha256sum Notes-dont-commit/AAPL.csv
```

On macOS use `shasum -a 256` instead.

## 3. Generate the forecast

Start with the defaults: historical drift and sample volatility.

```bash
./build/mcprice forecast --csv Notes-dont-commit/AAPL.csv --horizon-days 20
```

Try recent-weighted volatility and a 50% reduction in estimated GBM drift:

```bash
./build/mcprice forecast --csv Notes-dont-commit/AAPL.csv \
  --horizon-days 20 --volatility-model ewma --ewma-decay 0.94 \
  --drift-model shrinkage --drift-shrinkage 0.5
```

`forecast` fits every row in the supplied file. To fit only the latest year,
prepare a separate file containing its header and latest 253 daily prices.
There is no forecast lookback flag. A 20-day horizon means 20 trading sessions
after the final row, even when that row is years old.

The expected price is the model's mean; the median is its middle outcome. The
95% model interval describes future-price uncertainty under fitted assumptions.
It excludes parameter uncertainty, news, jumps, and changes in market behavior.
The probability-above-current field refers to a strict price increase.
For a flat deterministic forecast, that probability is zero.

Bootstrap intervals are unavailable with fewer than ten effective blocks.
Wilson directional intervals assume independent trials; shared training windows
and overlapping targets can violate that assumption. Treat them as descriptive,
not evidence of a trading edge.

The forecast command prints the 95% interval. Backtests additionally score 80%
and 90% intervals. Annualized drift and volatility use 252 sessions by default;
values printed as percentages differ from decimal inputs to the pricing CLI.

## 4. Test historical accuracy

Use a rolling year of returns to predict 20 sessions ahead, advancing by 20
sessions so target return periods do not overlap:

```bash
./build/mcprice backtest --csv Notes-dont-commit/AAPL.csv \
  --lookback-days 252 --horizon-days 20 --step-days 20 \
  --volatility-model ewma --ewma-decay 0.94 \
  --drift-model shrinkage --drift-shrinkage 0.5 \
  --bootstrap-samples 10000 --bootstrap-seed 42
```

Compare horizons by changing both `--horizon-days` and `--step-days` to 1, 5,
and 20. Compare drift modes with `historical`, `zero`, and `shrinkage`.
Pass `--drift-shrinkage` only with shrinkage and `--ewma-decay` only with EWMA.
Freeze your choices before evaluating a final holdout period; repeatedly picking
the best settings from the same test history overstates their accuracy.

Interpret the report as follows:

| Measure | What to look for |
| --- | --- |
| MAE, RMSE, MAPE | Smaller errors than the latest-price baseline |
| Paired MAE improvement | Positive favors the selected model; an interval spanning zero is inconclusive |
| Directional accuracy | Check the forecast count and Wilson interval, not just the percentage |
| Brier score | Lower is better; a constant 50% forecast always scores 0.25 |
| Calibration | Mean forecast probabilities should resemble observed upward frequencies |
| Interval coverage | Compare with the nominal 80%, 90%, or 95% rate |
| Width and interval score | Lower is better, assessed at the same horizon and price scale |

Many observations inside a very wide range do not establish good prediction.
Non-overlapping targets still share training data and may remain dependent.
Current Wilson and bootstrap intervals are approximations; tiny samples cannot
support strong conclusions even if the CLI labels the result better or worse.

## 5. Save a reproducible run

```bash
./build/mcprice backtest --csv Notes-dont-commit/AAPL.csv \
  --lookback-days 252 --horizon-days 20 --step-days 20 \
  --bootstrap-samples 10000 --bootstrap-seed 42 \
  > Notes-dont-commit/AAPL-backtest.txt
git rev-parse HEAD
c++ --version
```

Keep the exact command, data checksum, source revision, and compiler version.
Reports are plain text; JSON/CSV report export is not currently available.
The bootstrap seed controls resampling uncertainty, not the analytical GBM
forecast. A fixed full configuration reproduces on the same toolchain.

## Troubleshooting

- Missing column: use the exact header spelling with `--price-column`.
- Invalid date/order: convert dates to ISO format and sort oldest first.
- Invalid price: remove missing values and formatting, then investigate gaps.
- History too short: supply more daily rows or reduce lookback/horizon.
- Configure cannot download Catch2: restore network access or install Catch2 3.
- Build fails on C++20 library features: use a newer compiler and standard library.
- Forecast seems stale: check the final CSV date; no live quote is fetched.
- `nan` Monte Carlo uncertainty: one independent observation cannot estimate variance.

Option pricing is a separate command. Its risk-neutral rate is not the
historical drift used above. Greeks are currently exposed through the C++ API
and benchmarks, rather than forecast or pricing CLI flags.
