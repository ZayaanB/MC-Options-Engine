# Empirical forecast ranges

Day 38 adds ranges around the fixed [50/50 ensemble](ensemble-forecast.md).
It changes uncertainty estimates, not the point forecast or C++ option pricing.
Python 3.9+ is enough; no extra packages are needed.

```bash
python3 python/residual_forecasts.py \
  --manifest Notes-dont-commit/benchmark.json \
  --output Notes-dont-commit/residual-validation.json
```

At each horizon, forecasts start after ridge's training warmup and advance by
that horizon. Once a previous target close is observed, record
`log(actual) - log(previous forecast)`. Keep the latest 60 realized errors;
require 20 before reporting ranges. Pre-validation history supplies warmup.
At a 20-session horizon, this needs roughly 400 extra sessions after the first
fit. Short exports fail clearly rather than fabricate a range.

For each nominal 80/90/95% range, take interpolated equal-tail quantiles of those
errors, exponentiate and multiply by the current forecast. Do not recenter the
errors: they can reflect past bias. The point forecast stays unchanged and need
not lie inside the range. The empirical probability of rising counts strict
exceedances after shifting those same errors to the current forecast.

Latest price receives its own residual ranges on exactly the same dates and
calibration windows. JSON reports point errors, Brier scores versus a 50%
baseline, coverage, width and interval score (lower is better). A miss adds a
penalty to interval width. Calibration counts and first/last target dates make
the no-lookahead boundary visible. Source and input hashes identify each run.

The whole source is checksum-validated, but holdout rows never enter fitting,
calibration or scoring. Earlier validation errors can enter subsequent ranges
after their outcomes become known. Grid anchoring and warmup differ from Day 37;
compare paired rows here, not headline metrics across reports.

These are empirical prediction ranges, not Monte Carlo confidence intervals,
conformal guarantees or independently calibrated probabilities. Twenty to sixty
errors poorly resolve tails; regime changes and dependent errors can invalidate
coverage. A nominal 95% label must be checked against realized coverage and width.
GARCH remains a separate future experiment, not a dependency added here.

Reference: [prediction intervals and distributional evaluation](https://otexts.com/fpp3/toolbox.html).
