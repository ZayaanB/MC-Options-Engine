# Mathematics

The engine assumes zero dividends, a flat continuously compounded rate,
constant volatility, frictionless markets, and risk-neutral GBM:

```math
dS_t=rS_t\,dt+\sigma S_t\,dW_t.
```

## Prices and paths

European options use the exact terminal value

```math
S_T=S_0\exp\left((r-\tfrac12\sigma^2)T+\sigma\sqrt{T}Z\right),
\qquad Z\sim N(0,1),
```

with discounted payoffs `e^{-rT} max(S_T-K,0)` for calls and
`e^{-rT} max(K-S_T,0)` for puts.

An Asian path advances with `Delta t = T/M`:

```math
S_{j+1}=S_j\exp\left((r-\tfrac12\sigma^2)\Delta t
+\sigma\sqrt{\Delta t}Z_j\right).
```

Its average is `A_M = M^{-1} sum_{j=1}^M S_{jT/M}`. This excludes `S_0` and
includes maturity.

## Estimator and uncertainty

For `n` independent discounted payoff observations,

```math
\hat V=\frac1n\sum X_i,
\qquad s^2=\frac1{n-1}\sum(X_i-\hat V)^2,
\qquad SE=\frac{s}{\sqrt n}.
```

The reported interval is `V_hat ± 1.96 SE`. Welford's recurrence updates the
mean and centered sum of squares online, and its merge formula combines worker
summaries without storing individual payoffs.

Antithetic mode uses

```math
Y_i=\frac{X(Z_i)+X(-Z_i)}2.
```

With `N` requested trajectories, there are `N/2` independent pair averages.
Variance and standard error use that pair count. Odd `N` is rejected.

## Analytical check

For positive volatility and maturity,

```math
d_1=\frac{\ln(S_0/K)+(r+\tfrac12\sigma^2)T}{\sigma\sqrt T},
\qquad d_2=d_1-\sigma\sqrt T,
```

```math
C=S_0\Phi(d_1)-Ke^{-rT}\Phi(d_2),
\qquad
P=Ke^{-rT}\Phi(-d_2)-S_0\Phi(-d_1).
```

These formulas validate European Monte Carlo prices. The project does not
include an analytical arithmetic-Asian price.

## Greeks

Central differences estimate

```math
\Delta\approx\frac{V(S+h)-V(S-h)}{2h},
\qquad
\Gamma\approx\frac{V(S+h)-2V(S)+V(S-h)}{h^2},
```

```math
\text{Vega}\approx0.01\frac{V(\sigma+k)-V(\sigma-k)}{2k}.
```

When `sigma < k`, Vega uses the second-order forward difference
`(-3V(sigma) + 4V(sigma+k) - V(sigma+2k))/(2k)` instead.
Defaults are `h = 0.01S` and `k = 0.01`. The `0.01` factor reports Vega per one
volatility percentage point. Every bumped valuation reuses the same random
streams, which removes much of the noise from the differences.

Zero maturity and zero volatility are valid pricing cases, and negative finite
rates are allowed. Symmetric Vega is unavailable at zero volatility because the
down bump would leave the model domain.

Confidence intervals describe simulation noise under this model. They do not
cover model risk, parameter error, or finite-difference bias.

## Historical forecast

The forecast tool fits daily log returns
`x_t = log(S_t / S_{t-1})`. Their sample mean and volatility define a lognormal
price distribution over `h` trading days:

```math
S_{t+h}=S_t\exp(h\bar{x}+s\sqrt{h}Z).
```

This is a physical historical model, not the risk-neutral process used for
option pricing. Its 95% range describes the fitted model and does not include
parameter or structural uncertainty.

The optional EWMA estimate uses normalized exponential weights:

```math
s_{EWMA}^2=
\frac{\sum_{j=1}^{n}\lambda^{n-j}(x_j-\bar{x})^2}
{\sum_{j=1}^{n}\lambda^{n-j}},
\qquad 0<\lambda<1.
```

The default decay is `0.94`. Lower values react faster to recent volatility.
EWMA changes the forecast distribution, not risk-neutral pricing volatility.

For historical annualized GBM drift `mu` and shrinkage strength `a` in `[0,1]`,
the forecast uses

```math
\mu_{shrunk}=(1-a)\mu.
```

Zero retains the historical drift and one produces zero expected price growth.
The zero and historical drift modes are the two endpoints.

For forecasts `F_i`, realized prices `A_i`, and origin prices `S_i`, the
walk-forward report compares GBM errors with the baseline `B_i = S_i`:

```math
MAE=\frac1n\sum|F_i-A_i|,
\qquad RMSE=\sqrt{\frac1n\sum(F_i-A_i)^2}.
```

MAPE scales absolute error by `A_i`. Directional accuracy compares the signs of
`F_i-S_i` and `A_i-S_i`; coverage is the share of realized prices inside the
model's 95% interval. Every fit ends at its forecast origin, so future values
never enter its training window.

Momentum extends the trailing 20-day log-price change across the forecast
horizon. Mean reversion moves log price toward that window's mean, with the
fraction capped at one. Shorter lookbacks cap both benchmark windows. Neither
baseline is used by the pricing engine.

The paired MAE improvement observation is
`|B_i-A_i|-|F_i-A_i|`; positive values favor the model. Its 95% percentile
interval uses deterministic moving-block resampling. The block length is
`ceil(horizon/step)`, capped by the sample count. An interval containing zero is
reported as inconclusive. Directional accuracy uses a 95% Wilson interval.

For probability `p_i` of finishing above the origin price and binary outcome
`y_i`, the Brier score is the mean of `(p_i-y_i)^2`; lower is better.
Calibration buckets compare mean probabilities with observed frequencies. For
a central interval `[L,U]` with tail probability `alpha`, the interval score is
its width plus a `2/alpha` penalty for misses below `L` or above `U`.
