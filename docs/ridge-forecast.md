# Rolling return regression

Day 36 adds an experimental Python ridge model, separate from C++ option pricing.
It uses the same frozen multi-stock manifest as the [benchmark](forecast-benchmark.md).
Python 3.9+ is enough; no extra packages are needed.

```bash
python3 python/ridge_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --output Notes-dont-commit/ridge-validation.json
```

The runner evaluates validation only at 1, 5 and 20-session horizons, stepping
by the horizon. Allow at least 252 sessions of earlier history for the usual
252-session manifest lookback. Shorter windows must still supply 20 matured
labels plus a 20-return feature warmup. Every stock/horizon needs scored rows.

Features are the latest log return and the trailing 5/20-session mean log returns.
Each origin refits a separate direct-horizon regression on examples beginning
within the trailing lookback. Only labels ending on or before that origin enter
training; earlier validation outcomes may enter later fits once observed.
Scaling uses training-only means and population standard deviations. Constant
features use scale one. The intercept is unpenalized; slopes minimize summed
squared log-return error plus their squared magnitude (fixed penalty 1.0).
There is no hyperparameter search or random split.

The point forecast is current price times the exponential of the predicted log
return, not an estimate of expected arithmetic price. JSON includes each point,
training counts, hashes, MAE/RMSE and directional accuracy. Unchanged forecasts
are excluded from directional accuracy; unchanged actuals are neither up nor
down. No probabilities, prediction intervals or significance claims are supplied.

Compare ridge and latest price on the paired dates in this report. Other reports
may use different origins, so do not compare their headline errors directly.
Repeated validation experiments can overfit; keep the final holdout untouched.
Success on synthetic data checks mechanics, not stock-prediction skill.

Method references: [ridge objective](https://scikit-learn.org/stable/modules/linear_model.html#ridge-regression-and-classification)
and [chronological evaluation](https://scikit-learn.org/stable/modules/cross_validation.html#time-series-split).
