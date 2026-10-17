# ARIMA and ETS experiments

These optional Python challengers do not change the C++ engine or the Day 39
frozen ensemble. They use the same frozen manifest, validation split and paired
origins. Holdout rows never enter fitting or scoring.

```bash
.venv/bin/python -m pip install -r python/requirements-forecast.txt
.venv/bin/python python/classical_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --output Notes-dont-commit/classical-validation.json
.venv/bin/python tests/test_classical_forecasts.py
```

Use a fresh output path. The manifest lookback must be at least 30 sessions.
The runner fits the trailing lookback plus one close at every forecast origin:

- ARIMA(1,1,0), without drift.
- ETS(A,N,N): additive errors, level only.
- ETS(A,Ad,N): additive errors and damped additive trend, without seasonality.

All fits use log prices centered on the last training close. Predictions are
exponentiated fitted log values, not expected arithmetic prices. Flat windows
return the latest price exactly, avoiding degenerate optimization. Optimizer
iterations are capped at 200. There is no automatic order or seasonal search.

Every model and the latest-price baseline use identical 1/5/20-session dates.
JSON records point errors, direction counts, dependency/source/input hashes,
runtime, warnings and failed fits. Any failed fit disqualifies that candidate
globally; group metrics are unavailable rather than silently dropping dates or
substituting baseline prices. The ranking averages stock/horizon MAE ratios.
Latest price wins ties; both-zero groups are excluded. A nonzero error against
a perfect baseline makes that candidate ineligible.

No prediction intervals or significance claims are supplied. Selecting a new
model would require a separate procedure and genuinely untouched final data;
do not use the existing ensemble holdout results to tune these experiments.
The core tests need no statsmodels. Model-dependent tests explicitly skip when
it is absent; run the command above to exercise the actual optional models.

APIs: [statsmodels ARIMA](https://www.statsmodels.org/stable/generated/statsmodels.tsa.arima.model.ARIMA.html)
and [ETSModel](https://www.statsmodels.org/stable/generated/statsmodels.tsa.exponential_smoothing.ets.ETSModel.html).
