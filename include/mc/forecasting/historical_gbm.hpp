#pragma once

#include <cstddef>
#include <span>

namespace mc::forecasting {

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

[[nodiscard]] PriceForecast forecast_price(const HistoricalGbmModel& model,
                                           double current_price,
                                           std::size_t horizon_days);

}
