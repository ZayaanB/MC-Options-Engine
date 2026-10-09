# Forecast model research

Reviewed October 8, 2026. These are experiment priorities, not promises of stock
prediction accuracy. Keep optional Python models separate from C++ pricing.

| Model | Fit for this project | Suggested experiment |
| --- | --- | --- |
| ARIMA | Good inexpensive baseline; models serial dependence. | Small fixed low-order grid on log prices/returns, including random walk. |
| ETS | Good inexpensive smoothing baseline. | Level-only and damped trend; no assumed equity seasonality. |
| Meta Prophet | Lower priority: designed around trend, holidays and strong seasonality. | Only test if validation supports those effects; potentially better suited to volume than prices. |
| LightGBM | Worth a later feature-based ML experiment. | Lagged returns, rolling volatility and observed market features; shallow trees and tight tuning budget. |
| XGBoost | Similar role to LightGBM; choose one first. | Same features and chronological folds for a fair comparison. |
| DeepAR | Larger pivot: learns across many related series. | Build a sufficiently large, point-in-time stock panel first. |
| Amazon Chronos | Interesting pretrained probabilistic challenger. | Start with a small CPU benchmark; pin weights and check training-data overlap. Chronos-2 supports covariates. |
| Google TimesFM | Interesting pretrained challenger, not evidence of equity skill. | Pin checkpoint, benchmark memory/latency, and check its weight license; version matters. |

Our priority judgment is ARIMA/ETS, then regularized returns and one tree model,
then pretrained challengers. Defer DeepAR and Prophet unless the data warrants
them. Do not install everything or expand the tuning grid after seeing results.

TimesFM's current notice distinguishes Apache-2.0 code/weights through 2.5 from
3.0 downloaded weights, which are non-commercial/non-production only. Commercial
3.0 access through authorized Google Cloud services has separate terms.

Use identical stocks, origins, horizons and baselines. Fit transformations and
hyperparameters inside chronological training/validation folds. Remove training
labels whose future target crosses a fold boundary. Future regressors must be
known at the forecast origin—not realized future prices or volumes.

Compare MAE ratios, directional/Brier baselines, interval coverage/width and
runtime. Require robustness across assets and periods, then one untouched final
test. Foundation-model pretraining may include old benchmark histories: local
splits alone cannot rule that out. Check disclosed training sources/cutoffs and
prefer post-checkpoint data where possible. These precautions still do not
guarantee an edge over the latest-price baseline.

## Primary sources

- [ARIMA](https://otexts.com/fpp3/arima.html) and [exponential smoothing](https://otexts.com/fpp3/expsmooth.html), by forecasting researchers Hyndman and Athanasopoulos.
- [Prophet](https://facebook.github.io/prophet/) and [regressor requirements](https://facebook.github.io/prophet/docs/seasonality%2C_holiday_effects%2C_and_regressors.html).
- [LightGBM documentation](https://lightgbm.readthedocs.io/en/stable/) and [XGBoost documentation](https://xgboost.readthedocs.io/en/stable/).
- [DeepAR paper](https://arxiv.org/abs/1704.04110): probabilistic recurrent model trained across related series.
- [Amazon Chronos repository](https://github.com/amazon-science/chronos-forecasting): model families differ in covariate support, size and inference cost.
- [Google TimesFM repository](https://github.com/google-research/timesfm): version-specific APIs and pretrained-weight license notices. Do not assume the code license covers every checkpoint.
