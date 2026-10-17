# Boosted return experiment

This optional LightGBM challenger predicts direct 1/5/20-session log returns.
It does not replace the C++ CLI, ridge, ensemble or frozen final procedure.

```bash
.venv/bin/python -m pip install -r python/requirements-ml.txt
.venv/bin/python python/boosted_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --output Notes-dont-commit/boosted-validation.json
.venv/bin/python tests/test_boosted_forecasts.py
```

Use fresh output names and a manifest lookback of at least 60. At each origin,
training examples begin within the trailing lookback and all labels end on or
before that origin. Features use only observed 1/2/5/10/20-session log returns,
5/20-return population volatility, and distance from the trailing 20-close mean
log price. Allow 20 sessions of feature warmup and at least 40 matured labels.

Settings are fixed: L1 log-return loss, 80 rounds, learning rate 0.03, depth 3,
seven leaves, at least 20 rows/leaf, L2 regularization 1.0, and 63 histogram bins.
One CPU thread, seed 42, deterministic mode and forced column-wise processing
support repeatability on the same dependency/toolchain configuration. No GPU,
scikit-learn, early stopping or validation-parameter search is required.

The paired report includes MAE/RMSE, direction accuracy versus always-up,
bootstrap block sensitivity, training counts and descriptive feature gains.
Unchanged point forecasts abstain from direction scoring. The primary score is
the equal-weight stock/horizon mean MAE ratio against latest price. Both-zero
groups are excluded; failure against a perfect baseline makes it unavailable.
Latest price wins ties. Selection is descriptive, not an automatic deployment.

Holdout rows never enter fitting or scoring. Failed fits stop the run rather
than fabricate predictions. Outputs never overwrite previous reports.
Only point forecasts are supplied: exponentiating predicted log returns does
not produce expected arithmetic prices or calibrated probabilities.

This is a testable hypothesis, not an accuracy upgrade. Price-only inputs may
contain little usable signal. Our previously inspected Apple history is
exploratory; do not tune until it looks good and call that out-of-sample proof.
New features or models need fresh chronological evaluation and ultimately a
genuinely untouched period. Optional native-model tests skip without LightGBM;
run the virtual-environment test command to exercise them.

API and reproducibility details: [LightGBM parameters](https://lightgbm.readthedocs.io/en/stable/Parameters.html).
