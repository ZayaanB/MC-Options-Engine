#pragma once

#include <cstddef>
#include <span>

namespace mc::forecasting {

enum class VolatilityEstimator { sample, ewma };
enum class DriftEstimator { historical, zero, shrinkage };

struct HistoricalGbmModel {
    std::size_t return_observations{};
    double mean_daily_log_return{};
    double daily_volatility{};
    double annualized_drift{};
    double annualized_volatility{};
    double trading_days_per_year{};
};

struct PriceForecast {
    double current_price{};
    std::size_t horizon_days{};
    double expected_price{};
    double median_price{};
    double lower_95{};
    double upper_95{};
    double probability_above_current{};
};

[[nodiscard]] HistoricalGbmModel estimate_historical_gbm(
    std::span<const double> adjusted_closes,
    double trading_days_per_year = 252.0);

[[nodiscard]] HistoricalGbmModel estimate_ewma_gbm(
    std::span<const double> adjusted_closes,
    double decay = 0.94,
    double trading_days_per_year = 252.0);

[[nodiscard]] HistoricalGbmModel estimate_gbm(
    std::span<const double> adjusted_closes,
    VolatilityEstimator volatility_estimator,
    double ewma_decay = 0.94,
    double trading_days_per_year = 252.0,
    DriftEstimator drift_estimator = DriftEstimator::historical,
    double drift_shrinkage = 0.5);

[[nodiscard]] PriceForecast forecast_price(const HistoricalGbmModel& model,
                                           double current_price,
                                           std::size_t horizon_days);

}
