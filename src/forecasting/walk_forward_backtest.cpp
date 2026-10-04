#include "mc/forecasting/walk_forward_backtest.hpp"

#include <cmath>
#include <stdexcept>

#include "mc/forecasting/historical_gbm.hpp"
#include "mc/statistics/running_statistics.hpp"

namespace mc::forecasting {
namespace {

int direction(const double change) noexcept {
    if (change > 0.0) {
        return 1;
    }
    if (change < 0.0) {
        return -1;
    }
    return 0;
}

void validate_request(const std::span<const double> adjusted_closes,
                      const BacktestConfig& config) {
    if (config.lookback_days < 2) {
        throw std::invalid_argument{"backtest lookback must be at least two days"};
    }
    if (config.horizon_days == 0) {
        throw std::invalid_argument{"backtest horizon must be positive"};
    }
    if (config.step_days == 0) {
        throw std::invalid_argument{"backtest step must be positive"};
    }
    if (!std::isfinite(config.trading_days_per_year) ||
        config.trading_days_per_year <= 0.0) {
        throw std::invalid_argument{"trading days per year must be finite and positive"};
    }
    if (config.volatility_estimator == VolatilityEstimator::ewma &&
        (!std::isfinite(config.ewma_decay) || config.ewma_decay <= 0.0 ||
         config.ewma_decay >= 1.0)) {
        throw std::invalid_argument{"EWMA decay must be finite and between zero and one"};
    }
    if (adjusted_closes.size() <= config.lookback_days ||
        config.horizon_days > adjusted_closes.size() - config.lookback_days - 1) {
        throw std::invalid_argument{"price history is too short for the backtest configuration"};
    }
    for (const double price : adjusted_closes) {
        if (!std::isfinite(price) || price <= 0.0) {
            throw std::invalid_argument{"adjusted closes must be finite and positive"};
        }
    }
}

}

BacktestResult walk_forward_backtest(const std::span<const double> adjusted_closes,
                                    const BacktestConfig& config) {
    validate_request(adjusted_closes, config);

    BacktestResult result;
    RunningStatistics absolute_errors;
    RunningStatistics squared_errors;
    RunningStatistics percentage_errors;
    RunningStatistics baseline_absolute_errors;
    RunningStatistics baseline_squared_errors;
    RunningStatistics baseline_percentage_errors;
    RunningStatistics interval_widths;
    std::size_t correct_directions = 0;
    std::size_t covered_intervals = 0;

    const std::size_t last_origin = adjusted_closes.size() - config.horizon_days - 1;
    for (std::size_t origin = config.lookback_days; origin <= last_origin;) {
        const auto training = adjusted_closes.subspan(
            origin - config.lookback_days, config.lookback_days + 1);
        const auto model = estimate_gbm(training, config.volatility_estimator,
                                        config.ewma_decay,
                                        config.trading_days_per_year);
        const auto forecast =
            forecast_price(model, adjusted_closes[origin], config.horizon_days);
        const std::size_t target = origin + config.horizon_days;
        const double actual = adjusted_closes[target];
        const double error = forecast.expected_price - actual;
        const double baseline_error = adjusted_closes[origin] - actual;
        const double squared_error = error * error;
        const double percentage_error = std::abs(error) / actual;
        const double baseline_squared_error = baseline_error * baseline_error;
        const double baseline_percentage_error = std::abs(baseline_error) / actual;
        if (!std::isfinite(squared_error) || !std::isfinite(percentage_error) ||
            !std::isfinite(baseline_squared_error) ||
            !std::isfinite(baseline_percentage_error)) {
            throw std::overflow_error{"backtest error exceeds the finite numeric range"};
        }

        absolute_errors.add(std::abs(error));
        squared_errors.add(squared_error);
        percentage_errors.add(percentage_error);
        baseline_absolute_errors.add(std::abs(baseline_error));
        baseline_squared_errors.add(baseline_squared_error);
        baseline_percentage_errors.add(baseline_percentage_error);
        interval_widths.add(forecast.upper_95 - forecast.lower_95);

        if (direction(forecast.expected_price - adjusted_closes[origin]) ==
            direction(actual - adjusted_closes[origin])) {
            ++correct_directions;
        }
        if (actual >= forecast.lower_95 && actual <= forecast.upper_95) {
            ++covered_intervals;
        }
        result.points.push_back(BacktestPoint{origin,
                                              target,
                                              adjusted_closes[origin],
                                              forecast.expected_price,
                                              actual,
                                              forecast.lower_95,
                                              forecast.upper_95});

        if (config.step_days > last_origin - origin) {
            break;
        }
        origin += config.step_days;
    }

    const double count = static_cast<double>(result.points.size());
    result.mean_absolute_error = absolute_errors.mean();
    result.root_mean_squared_error = std::sqrt(squared_errors.mean());
    result.mean_absolute_percentage_error = percentage_errors.mean();
    result.baseline_mean_absolute_error = baseline_absolute_errors.mean();
    result.baseline_root_mean_squared_error =
        std::sqrt(baseline_squared_errors.mean());
    result.baseline_mean_absolute_percentage_error =
        baseline_percentage_errors.mean();
    result.directional_accuracy = static_cast<double>(correct_directions) / count;
    result.interval_coverage = static_cast<double>(covered_intervals) / count;
    result.mean_interval_width = interval_widths.mean();
    return result;
}

}
