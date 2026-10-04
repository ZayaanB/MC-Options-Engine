#pragma once

#include <cstddef>
#include <span>
#include <vector>

namespace mc::forecasting {

struct BacktestConfig {
    std::size_t lookback_days{252};
    std::size_t horizon_days{20};
    std::size_t step_days{1};
    double trading_days_per_year{252.0};
};

struct BacktestPoint {
    std::size_t origin_index{};
    std::size_t target_index{};
    double current_price{};
    double forecast_price{};
    double actual_price{};
    double lower_95{};
    double upper_95{};
};

struct BacktestResult {
    std::vector<BacktestPoint> points;
    double mean_absolute_error{};
    double root_mean_squared_error{};
    double mean_absolute_percentage_error{};
    double baseline_mean_absolute_error{};
    double baseline_root_mean_squared_error{};
    double baseline_mean_absolute_percentage_error{};
    double directional_accuracy{};
    double interval_coverage{};
    double mean_interval_width{};
};

[[nodiscard]] BacktestResult walk_forward_backtest(
    std::span<const double> adjusted_closes,
    const BacktestConfig& config = {});

}
