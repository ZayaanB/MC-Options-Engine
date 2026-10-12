# Fixed-weight forecast ensemble

Day 37 averages the [ridge point forecast](ridge-forecast.md) and latest price,
50/50 in price space. The weights are fixed, not fitted or selected by searching
validation results. Python 3.9+ is enough; no extra packages are needed.

```bash
python3 python/ensemble_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --output Notes-dont-commit/ensemble-validation.json
```

It uses ridge's frozen data, training boundaries and validation origins. Ridge,
ensemble and latest price are scored on exactly the same stock/horizon/date
groups. Holdout rows never enter model fitting or scoring. Earlier validation
outcomes can enter later fits after they become observable.

JSON includes all points, MAE/RMSE, directional counts/accuracy, fixed weights
and source/input hashes. `ridge_predicted_log_return` describes the ridge
component, not the blended price. There are no fitted probabilities or intervals.
Unchanged forecasts abstain from direction scoring; unchanged actuals are
neither up nor down. A positive-weight blend usually has the same direction as
ridge, so smaller price errors need not mean better directional predictions.

The descriptive ranking equally weights each stock/horizon MAE ratio against
latest price. Both-zero groups are excluded; an error against a perfect baseline
disqualifies a candidate. Ties favor latest price, then ensemble, then ridge.
An all-flat dataset selects latest price. Ranking does not change ensemble
weights or the C++ CLI's forecast behavior.

Blending limits how far ridge moves from the current price. It does not create
new information or guarantee an accuracy gain. Validation ranking is not a
significance test; freeze the procedure before any final untouched evaluation.
