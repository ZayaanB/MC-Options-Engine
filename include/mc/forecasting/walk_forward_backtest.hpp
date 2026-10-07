#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "mc/forecasting/historical_gbm.hpp"

namespace mc::forecasting {

struct BacktestConfig {
    std::size_t lookback_days{252};
    std::size_t horizon_days{20};
    std::size_t step_days{1};
    double trading_days_per_year{252.0};
    VolatilityEstimator volatility_estimator{VolatilityEstimator::sample};
    double ewma_decay{0.94};
    DriftEstimator drift_estimator{DriftEstimator::historical};
    double drift_shrinkage{0.5};
    std::size_t bootstrap_samples{10'000};
    std::uint64_t bootstrap_seed{42};
};

struct BacktestPoint {
    std::size_t origin_index{};
    std::size_t target_index{};
    double current_price{};
    double forecast_price{};
    double actual_price{};
    double lower_95{};
    double upper_95{};
    double latest_price_forecast{};
    double historical_drift_forecast{};
    double zero_drift_forecast{};
    double momentum_forecast{};
    double mean_reversion_forecast{};
};

struct ForecastErrorMetrics {
    double mean_absolute_error{};
    double root_mean_squared_error{};
    double mean_absolute_percentage_error{};
    std::size_t directional_predictions{};
    std::size_t directionally_correct{};
    double directional_accuracy{};
    double directional_lower_95{};
    double directional_upper_95{};
};

enum class ComparisonConclusion { better, worse, inconclusive };

struct MaeImprovementEstimate {
    double absolute_improvement{};
    double relative_improvement{};
    double lower_95{};
    double upper_95{};
    std::size_t bootstrap_samples{};
    std::uint64_t bootstrap_seed{};
    std::size_t block_length{};
    ComparisonConclusion conclusion{ComparisonConclusion::inconclusive};
};

struct BacktestResult {
    std::vector<BacktestPoint> points;
    ForecastErrorMetrics selected_model;
    ForecastErrorMetrics latest_price;
    ForecastErrorMetrics historical_drift;
    ForecastErrorMetrics zero_drift;
    ForecastErrorMetrics momentum;
    ForecastErrorMetrics mean_reversion;
    MaeImprovementEstimate mae_improvement;
    double interval_coverage{};
    double mean_interval_width{};
};

[[nodiscard]] BacktestResult walk_forward_backtest(
    std::span<const double> adjusted_closes,
    const BacktestConfig& config = {});

}
