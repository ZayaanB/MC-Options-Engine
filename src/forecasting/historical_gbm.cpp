#include "mc/forecasting/historical_gbm.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "mc/statistics/running_statistics.hpp"

namespace mc::forecasting {
namespace {

constexpr double kNormal80 = 1.2815515655446004;
constexpr double kNormal90 = 1.6448536269514722;
constexpr double kNormal95 = 1.959963984540054;

void require_positive_finite(const double value, const char* const name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument{std::string{name} + " must be finite and positive"};
    }
}

double standard_normal_cdf(const double value) {
    return 0.5 * std::erfc(-value / std::sqrt(2.0));
}

void require_finite_result(const double value) {
    if (!std::isfinite(value)) {
        throw std::overflow_error{"forecast exceeds the finite numeric range"};
    }
}

RunningStatistics log_return_statistics(
    const std::span<const double> adjusted_closes) {
    if (adjusted_closes.size() < 3) {
        throw std::invalid_argument{"at least three adjusted closes are required"};
    }

    RunningStatistics returns;
    require_positive_finite(adjusted_closes.front(), "adjusted close");
    for (std::size_t index = 1; index < adjusted_closes.size(); ++index) {
        require_positive_finite(adjusted_closes[index], "adjusted close");
        returns.add(std::log(adjusted_closes[index]) -
                    std::log(adjusted_closes[index - 1]));
    }
    return returns;
}

HistoricalGbmModel make_model(const std::size_t observations,
                              const double mean_daily_log_return,
                              const double daily_volatility,
                              const double trading_days_per_year) {
    const double annualized_volatility =
        daily_volatility * std::sqrt(trading_days_per_year);
    const double annualized_drift =
        mean_daily_log_return * trading_days_per_year +
        0.5 * annualized_volatility * annualized_volatility;

    require_finite_result(annualized_drift);
    require_finite_result(annualized_volatility);

    return HistoricalGbmModel{observations,
                              mean_daily_log_return,
                              daily_volatility,
                              annualized_drift,
                              annualized_volatility,
                              trading_days_per_year};
}

HistoricalGbmModel apply_drift_estimator(
    const HistoricalGbmModel& model, const DriftEstimator drift_estimator,
    const double drift_shrinkage) {
    double annualized_drift = model.annualized_drift;
    switch (drift_estimator) {
        case DriftEstimator::historical:
            break;
        case DriftEstimator::zero:
            annualized_drift = 0.0;
            break;
        case DriftEstimator::shrinkage:
            if (!std::isfinite(drift_shrinkage) || drift_shrinkage < 0.0 ||
                drift_shrinkage > 1.0) {
                throw std::invalid_argument{
                    "drift shrinkage must be finite and between zero and one"};
            }
            annualized_drift *= 1.0 - drift_shrinkage;
            break;
        default:
            throw std::invalid_argument{"unknown drift estimator"};
    }
    const double mean_daily_log_return =
        annualized_drift / model.trading_days_per_year -
        0.5 * model.daily_volatility * model.daily_volatility;
    return make_model(model.return_observations, mean_daily_log_return,
                      model.daily_volatility, model.trading_days_per_year);
}

}

HistoricalGbmModel estimate_historical_gbm(
    const std::span<const double> adjusted_closes,
    const double trading_days_per_year) {
    require_positive_finite(trading_days_per_year, "trading days per year");
    const RunningStatistics returns = log_return_statistics(adjusted_closes);

    const double daily_volatility = std::sqrt(returns.variance());
    return make_model(adjusted_closes.size() - 1, returns.mean(), daily_volatility,
                      trading_days_per_year);
}

HistoricalGbmModel estimate_ewma_gbm(
    const std::span<const double> adjusted_closes, const double decay,
    const double trading_days_per_year) {
    require_positive_finite(trading_days_per_year, "trading days per year");
    if (!std::isfinite(decay) || decay <= 0.0 || decay >= 1.0) {
        throw std::invalid_argument{"EWMA decay must be finite and between zero and one"};
    }

    const RunningStatistics returns = log_return_statistics(adjusted_closes);
    const double mean = returns.mean();
    double weighted_squared_deviations = 0.0;
    double weight_sum = 0.0;
    for (std::size_t index = 1; index < adjusted_closes.size(); ++index) {
        const double value =
            std::log(adjusted_closes[index]) - std::log(adjusted_closes[index - 1]);
        const double deviation = value - mean;
        weighted_squared_deviations =
            decay * weighted_squared_deviations + deviation * deviation;
        weight_sum = decay * weight_sum + 1.0;
    }
    const double variance = weighted_squared_deviations / weight_sum;
    require_finite_result(variance);
    return make_model(adjusted_closes.size() - 1, mean, std::sqrt(variance),
                      trading_days_per_year);
}

HistoricalGbmModel estimate_gbm(
    const std::span<const double> adjusted_closes,
    const VolatilityEstimator volatility_estimator, const double ewma_decay,
    const double trading_days_per_year, const DriftEstimator drift_estimator,
    const double drift_shrinkage) {
    HistoricalGbmModel model;
    switch (volatility_estimator) {
        case VolatilityEstimator::sample:
            model = estimate_historical_gbm(adjusted_closes,
                                            trading_days_per_year);
            break;
        case VolatilityEstimator::ewma:
            model = estimate_ewma_gbm(adjusted_closes, ewma_decay,
                                      trading_days_per_year);
            break;
        default:
            throw std::invalid_argument{"unknown volatility estimator"};
    }
    return apply_drift_estimator(model, drift_estimator, drift_shrinkage);
}

PriceForecast forecast_price(const HistoricalGbmModel& model,
                             const double current_price,
                             const std::size_t horizon_days) {
    require_positive_finite(current_price, "current price");
    require_positive_finite(model.trading_days_per_year, "trading days per year");
    if (model.return_observations < 2) {
        throw std::invalid_argument{"model requires at least two return observations"};
    }
    if (!std::isfinite(model.mean_daily_log_return) ||
        !std::isfinite(model.daily_volatility) || model.daily_volatility < 0.0 ||
        !std::isfinite(model.annualized_drift) ||
        !std::isfinite(model.annualized_volatility) || model.annualized_volatility < 0.0) {
        throw std::invalid_argument{"model parameters must be finite and valid"};
    }
    if (horizon_days == 0) {
        throw std::invalid_argument{"forecast horizon must be positive"};
    }

    const double horizon = static_cast<double>(horizon_days);
    const double log_mean = model.mean_daily_log_return * horizon;
    const double log_standard_deviation = model.daily_volatility * std::sqrt(horizon);
    const double median = current_price * std::exp(log_mean);
    const double expected =
        current_price * std::exp(log_mean + 0.5 * log_standard_deviation *
                                                log_standard_deviation);
    const double lower_80 =
        current_price * std::exp(log_mean - kNormal80 * log_standard_deviation);
    const double upper_80 =
        current_price * std::exp(log_mean + kNormal80 * log_standard_deviation);
    const double lower_90 =
        current_price * std::exp(log_mean - kNormal90 * log_standard_deviation);
    const double upper_90 =
        current_price * std::exp(log_mean + kNormal90 * log_standard_deviation);
    const double lower_95 =
        current_price * std::exp(log_mean - kNormal95 * log_standard_deviation);
    const double upper_95 =
        current_price * std::exp(log_mean + kNormal95 * log_standard_deviation);
    const double probability_above_current =
        log_standard_deviation == 0.0
            ? (log_mean > 0.0 ? 1.0 : 0.0)
            : standard_normal_cdf(log_mean / log_standard_deviation);

    require_finite_result(expected);
    require_finite_result(median);
    require_finite_result(lower_80);
    require_finite_result(upper_80);
    require_finite_result(lower_90);
    require_finite_result(upper_90);
    require_finite_result(lower_95);
    require_finite_result(upper_95);

    return PriceForecast{current_price,
                         horizon_days,
                         expected,
                         median,
                         lower_80,
                         upper_80,
                         lower_90,
                         upper_90,
                         lower_95,
                         upper_95,
                         probability_above_current};
}

}
