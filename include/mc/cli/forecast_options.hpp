#pragma once

#include <cstddef>
#include <span>
#include <string>
#include <string_view>

#include "mc/forecasting/historical_gbm.hpp"

namespace mc::cli {

struct ForecastOptions {
    std::string csv_path;
    std::string price_column{"Adj Close"};
    std::size_t horizon_days{20};
    double trading_days_per_year{252.0};
    forecasting::VolatilityEstimator volatility_estimator{
        forecasting::VolatilityEstimator::sample};
    double ewma_decay{0.94};
    forecasting::DriftEstimator drift_estimator{
        forecasting::DriftEstimator::historical};
    double drift_shrinkage{0.5};
};

[[nodiscard]] ForecastOptions parse_forecast_options(
    std::span<const std::string_view> arguments);

}
