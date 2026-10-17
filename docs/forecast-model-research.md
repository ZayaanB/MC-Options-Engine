# Forecast research

Reviewed October 17, 2026. This is an implementation-priority judgment, not a
promise of stock accuracy. Keep these experiments separate from option pricing.

## What our results mean

MC and Black-Scholes price options under stated assumptions; they do not predict
stock direction. Agreement between them checks numerical implementation, not
agreement with actual option quotes. Black-Scholes is a closed-form reference
within its model, not a trained predictor.

Our current LightGBM is a separate, price-only return regressor. Taking the sign
of its prediction is not the same as training a calibrated direction classifier.
Near-50% direction accuracy has no demonstrated value unless it beats relevant
baselines. In rising markets, even 55% can lose to always predicting up.
MAE, probability calibration and range coverage answer different questions.

## Papers checked

| Paper | Actual finding | What we can reasonably take from it |
| --- | --- | --- |
| [Fischer & Krauss, LSTM (2017 working paper; published 2018)](https://www.iwf.rw.fau.de/files/2015/12/11-2017.pdf) | 54.3% for selected top/bottom stocks outperforming the cross-sectional median; returns deteriorate in 2010–2015. | Evidence for pooled sequence learning, not 54.3% tomorrow-up accuracy on Apple. |
| [Gu, Kelly & Xiu, 2020](https://dachxiu.chicagobooth.edu/download/ML.pdf) | Trees/neural nets improve monthly return prediction; NN3 reaches 0.40% out-of-sample R-squared in the pooled sample. | Tiny predictive signal can matter across a large panel. Needs rich characteristics and careful timing. |
| [Jiang, Kelly & Xiu, 2023](https://doi.org/10.1111/jofi.13268) | CNNs learn from scaled OHLC/volume windows; train/validate in 1993–2000, test in 2001–2019. Their image-scaled logistic control is also useful. | Test normalization and simple classification before assuming a large network is necessary. Portfolio Sharpe is not direction accuracy. |
| [Namdari & Durrani, 2021](https://doi.org/10.1007/s43069-021-00071-2) | Reports 70.36% average directional accuracy across five discretized trend classes with technical/fundamental inputs. | Not our binary daily-up task. Fundamental availability and preprocessing would need an independent reproduction. |
| [Zhang, Zohren & Roberts, DeepLOB](https://arxiv.org/html/1808.03668v6) | 84.47% on FI-2010 setup 2, horizon 10; labels are up/stationary/down from smoothed order-book mid-prices. | Credible different task, not daily-close forecasting. Needs order-book data; the paper itself notes FI-2010 is too short for a realistic standalone evaluation. |
| [Pretrained models for financial returns, 2026 preprint](https://arxiv.org/html/2606.27100v1) | Foundation models often beat locally trained neural alternatives, but gains over random walk are sparse. Raw significant comparisons are exploratory. | Chronos/TimesFM are challengers, not proven universal stock predictors. Small test samples and pretraining overlap remain concerns. |
| [Rubattu, Maroni & Corani, 2023](https://arxiv.org/html/2305.05575v1) | Load competition: lagged features reduce qualification error; probabilistic LightGBM and temporal hierarchies improve uncertainty scores. | Supports a separate energy-demand experiment, not stock accuracy claims. Competition horizons differ from our proposed next-24-hour task. |

The GKX authors studied almost 30,000 stocks and 94 firm characteristics, plus
macroeconomic interactions and industry information. That is very different from
our few hundred trailing labels and eight price-derived inputs. Their
[public resources](https://dachxiu.chicagobooth.edu/) include data and simulation
examples; reproducing source characteristics can require licensed databases.
Do not execute third-party notebooks or unpickle downloads blindly.

The CNN paper also reports useful one-dimensional CNN and logistic alternatives.
Its randomly selected training/validation images are confined to the early
sample, with a later chronological test. We should still use purged temporal
validation in our own overlapping-window experiments.

## What to build next

1. **Feature-based LightGBM plus a regularized logistic control.** Use observed
   OHLC ranges, volume changes, market/sector returns and volatility. Compare
   price-return regression with a separate binary classifier; do not substitute
   classification accuracy for price-error evaluation.
2. **Pool a broader stock panel.** Share training across assets, split by dates
   across all assets, and remove labels crossing validation boundaries. A few
   current technology survivors are not a survivorship-free universe.
3. **A small 1D CNN or LSTM challenger.** Use consistent window normalization and
   the same data/splits. A 16 GB GPU is a reasonable experiment target, not a
   guarantee of fit, speed or better accuracy. Do not train a foundation model.
4. **Pretrained Chronos/TimesFM as a bounded comparison.** Pin checkpoint and
   dependencies, check license and possible pretraining overlap, and prefer
   genuinely post-checkpoint evaluation.
5. **Separate energy-demand LightGBM.** See the [energy guide](../apps/energy/README.md).
   Start with prior-day/week baselines and lagged demand/calendar inputs. Weather
   must be archived forecasts available at the origin, not realized future weather.

ARIMA/ETS, ridge, the ensemble and LightGBM regression are already implemented.
Prophet is lower priority for daily equity prices because its intended strengths
include strong seasonality. DeepAR/TFT need a richer panel to justify their
complexity. XGBoost/CatBoost are reasonable controlled challengers, not automatic
upgrades. No reviewed result establishes an absolutely best model for our task.

## Current data and evaluation

The [downloader](../apps/prediction/README.md) obtained 4,223 daily rows each for
AAPL, MSFT, GOOGL, SPY, QQQ and XLK: January 4, 2010 to October 16, 2026.
CSV, metadata, checksums and results stay under the ignored
`Notes-dont-commit/market-data-20261017/` folder.

The new fixed-settings comparison uses AAPL/MSFT/GOOGL, a 252-return lookback,
2018–2023 validation, and 1/5/20-session targets. C++ dense forecast rows were
filtered to exactly the LightGBM origin/target dates before comparison. No
model settings were tuned from these results. The 2024 onward rows were
downloaded but not fitted or scored; reserve them for a separately frozen final
procedure, not the old missing final benchmark.

GBM's equal-weight asset/horizon mean MAE ratio was 1.00067; LightGBM's was
1.06126, where unchanged-price baseline is 1.0. Neither demonstrates a useful
edge. Always-up beat both on aggregate direction at every horizon. Confidence
intervals must account for dependence; current block choices are heuristic and
multiple model searches are not corrected.

This is a new validation experiment, not evidence of accuracy improvement or
loss against the earlier 2015–2017 Apple diagnostic. Different periods, lookbacks
and adjustment snapshots are not directly comparable. The new OHLCV/market
columns are available but have not yet been used by the existing price-only ML
model. Provider adjustments and exchange-session completeness are not independently
verified. Historical adjusted prices do not supply point-in-time fundamentals.
