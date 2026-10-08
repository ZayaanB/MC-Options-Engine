# Multi-stock accuracy benchmark

The runner calls the C++ engine with historical drift, sample volatility, and
252 sessions/year. It evaluates 1, 5, and 20-session horizons with matching
steps, so target return windows do not overlap. It does not tune models.

## Freeze the experiment

1. Export daily histories and metadata for at least two stocks. Follow the
   [data workflow](forecast-workflow.md); keep private exports outside Git.
2. Copy `examples/forecast_benchmark.template.json` to
   `Notes-dont-commit/benchmark.json`. Paths are relative to that manifest.
3. Set the ticker list, lookback, and chronological split dates before testing.
   Use enough history before validation and enough sessions in both periods
   for the 20-session horizon. Every asset/horizon must have eligible forecasts.
4. Replace both checksum placeholders per stock with the output of
   `sha256sum FILE.csv FILE.meta` (macOS: `shasum -a 256`).

The manifest accepts exactly the template's fields. Changed input bytes,
duplicate tickers, invalid metadata, or empty evaluation groups fail the run.
Provenance is declared by you; hashes establish file identity, not data quality.

## Run validation

Requires Python 3.9+; no third-party Python packages are needed.

```bash
python3 python/benchmark_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --engine build/mcprice \
  --output Notes-dont-commit/validation.json
```

Validation is the default. Only rows before `holdout_start` reach the engine.
Both origin and target must be inside the evaluation period; boundary-crossing
forecasts are excluded. Origins remain anchored to the engine's first eligible
origin, not reset at each split. Previously observed holdout prices can enter
later training windows during a holdout run, as in a live rolling forecast.

The runner validates and hashes the whole source file, but does not score
holdout targets in validation mode. It snapshots the executable and input files
for each run. JSON records the manifest, input/executable hashes, platform,
per-stock metrics, and full-precision forecast rows. It never overwrites an
existing output and does not pool dollar errors across stocks.

## Run the final holdout

Freeze your procedure after validation, then run the final test explicitly:

```bash
python3 python/benchmark_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json --engine build/mcprice \
  --phase holdout --output Notes-dont-commit/holdout.json
```

This flag prevents accidental scoring, not deliberate repeated testing.
Editing settings after looking at holdout results makes that period validation
data; you need a new untouched period for a final test.

## Interpret the output

Compare selected-model MAE/RMSE against `latest_price`. Positive relative MAE
improvement favors the selected model. MAPE, direction accuracy, coverage, and
relative improvements are fractions, not percentages. Direction accuracy
excludes unchanged point forecasts and counts an unchanged actual as neither
up nor down. An unavailable statistic is JSON `null`, never zero.

Coverage alone is not enough: compare interval widths too. These are descriptive
results, not a significance test or proof of a predictive edge. Nonoverlapping
targets can still have dependent errors. The runner uses declared daily cadence,
not an exchange calendar, and cannot remove survivorship or corporate-action
bias from an unsuitable export.

For your own analysis, export the engine's individual predictions directly:

```bash
./build/mcprice backtest --csv examples/sample_prices.csv \
  --lookback-days 10 --horizon-days 3 --step-days 3 --format csv
```
