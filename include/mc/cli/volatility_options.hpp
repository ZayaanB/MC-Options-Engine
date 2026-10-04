#pragma once

#include <string_view>

#include "mc/forecasting/historical_gbm.hpp"

namespace mc::cli {

[[nodiscard]] forecasting::VolatilityEstimator parse_volatility_estimator(
    std::string_view text);
[[nodiscard]] double parse_ewma_decay(std::string_view text);
[[nodiscard]] std::string_view volatility_estimator_name(
    forecasting::VolatilityEstimator estimator) noexcept;

}
