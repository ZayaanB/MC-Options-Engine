#pragma once

#include <string_view>

#include "mc/forecasting/historical_gbm.hpp"

namespace mc::cli {

[[nodiscard]] forecasting::DriftEstimator parse_drift_estimator(
    std::string_view text);
[[nodiscard]] double parse_drift_shrinkage(std::string_view text);
[[nodiscard]] std::string_view drift_estimator_name(
    forecasting::DriftEstimator estimator) noexcept;

}
