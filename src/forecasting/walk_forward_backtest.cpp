#include "mc/forecasting/walk_forward_backtest.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
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

class MetricsAccumulator {
public:
    void add(const double forecast, const double current, const double actual) {
        if (!std::isfinite(forecast)) {
            throw std::overflow_error{
                "backtest forecast exceeds the finite numeric range"};
        }
        const double error = forecast - actual;
        const double squared_error = error * error;
        const double percentage_error = std::abs(error) / actual;
        if (!std::isfinite(squared_error) ||
            !std::isfinite(percentage_error)) {
            throw std::overflow_error{
                "backtest error exceeds the finite numeric range"};
        }
        absolute_errors_.add(std::abs(error));
        squared_errors_.add(squared_error);
        percentage_errors_.add(percentage_error);

        const int predicted_direction = direction(forecast - current);
        if (predicted_direction != 0) {
            ++directional_predictions_;
            if (predicted_direction == direction(actual - current)) {
                ++correct_directions_;
            }
        }
    }

    ForecastErrorMetrics result() const {
        const double directional_accuracy =
            directional_predictions_ == 0
                ? std::numeric_limits<double>::quiet_NaN()
                : static_cast<double>(correct_directions_) /
                      static_cast<double>(directional_predictions_);
        return ForecastErrorMetrics{
            absolute_errors_.mean(), std::sqrt(squared_errors_.mean()),
            percentage_errors_.mean(), directional_predictions_,
            directional_accuracy};
    }

private:
    RunningStatistics absolute_errors_;
    RunningStatistics squared_errors_;
    RunningStatistics percentage_errors_;
    std::size_t directional_predictions_{};
    std::size_t correct_directions_{};
};

double momentum_forecast(const std::span<const double> training,
                         const std::size_t window,
                         const std::size_t horizon) {
    const double current = training.back();
    const double anchor = training[training.size() - window - 1];
    const double scaled_log_return =
        std::log(current / anchor) * static_cast<double>(horizon) /
        static_cast<double>(window);
    return current * std::exp(scaled_log_return);
}

double mean_reversion_forecast(const std::span<const double> training,
                               const std::size_t window,
                               const std::size_t horizon) {
    const auto prices = training.last(window + 1);
    double mean_log_price = 0.0;
    for (const double price : prices) {
        mean_log_price += std::log(price);
    }
    mean_log_price /= static_cast<double>(prices.size());
    const double current_log_price = std::log(training.back());
    const double reversion_fraction = std::min(
        static_cast<double>(horizon) / static_cast<double>(window), 1.0);
    return std::exp(current_log_price +
                    reversion_fraction * (mean_log_price - current_log_price));
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
    if (config.drift_estimator == DriftEstimator::shrinkage &&
        (!std::isfinite(config.drift_shrinkage) ||
         config.drift_shrinkage < 0.0 || config.drift_shrinkage > 1.0)) {
        throw std::invalid_argument{
            "drift shrinkage must be finite and between zero and one"};
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
    MetricsAccumulator selected_metrics;
    MetricsAccumulator latest_price_metrics;
    MetricsAccumulator historical_drift_metrics;
    MetricsAccumulator zero_drift_metrics;
    MetricsAccumulator momentum_metrics;
    MetricsAccumulator mean_reversion_metrics;
    RunningStatistics interval_widths;
    std::size_t covered_intervals = 0;

    const std::size_t last_origin = adjusted_closes.size() - config.horizon_days - 1;
    for (std::size_t origin = config.lookback_days; origin <= last_origin;) {
        const auto training = adjusted_closes.subspan(
            origin - config.lookback_days, config.lookback_days + 1);
        const auto model = estimate_gbm(training, config.volatility_estimator,
                                        config.ewma_decay,
                                        config.trading_days_per_year,
                                        config.drift_estimator,
                                        config.drift_shrinkage);
        const auto forecast =
            forecast_price(model, adjusted_closes[origin], config.horizon_days);
        const auto historical_model = estimate_gbm(
            training, config.volatility_estimator, config.ewma_decay,
            config.trading_days_per_year, DriftEstimator::historical);
        const auto historical_forecast = forecast_price(
            historical_model, adjusted_closes[origin], config.horizon_days);
        const std::size_t target = origin + config.horizon_days;
        const double current = adjusted_closes[origin];
        const double actual = adjusted_closes[target];
        const std::size_t benchmark_window =
            std::min<std::size_t>(20, config.lookback_days);
        const double momentum = momentum_forecast(
            training, benchmark_window, config.horizon_days);
        const double mean_reversion = mean_reversion_forecast(
            training, benchmark_window, config.horizon_days);

        selected_metrics.add(forecast.expected_price, current, actual);
        latest_price_metrics.add(current, current, actual);
        historical_drift_metrics.add(historical_forecast.expected_price,
                                     current, actual);
        zero_drift_metrics.add(current, current, actual);
        momentum_metrics.add(momentum, current, actual);
        mean_reversion_metrics.add(mean_reversion, current, actual);
        interval_widths.add(forecast.upper_95 - forecast.lower_95);

        if (actual >= forecast.lower_95 && actual <= forecast.upper_95) {
            ++covered_intervals;
        }
        result.points.push_back(BacktestPoint{origin,
                                              target,
                                              adjusted_closes[origin],
                                              forecast.expected_price,
                                              actual,
                                              forecast.lower_95,
                                              forecast.upper_95,
                                              current,
                                              historical_forecast.expected_price,
                                              current,
                                              momentum,
                                              mean_reversion});

        if (config.step_days > last_origin - origin) {
            break;
        }
        origin += config.step_days;
    }

    const double count = static_cast<double>(result.points.size());
    result.selected_model = selected_metrics.result();
    result.latest_price = latest_price_metrics.result();
    result.historical_drift = historical_drift_metrics.result();
    result.zero_drift = zero_drift_metrics.result();
    result.momentum = momentum_metrics.result();
    result.mean_reversion = mean_reversion_metrics.result();
    result.interval_coverage = static_cast<double>(covered_intervals) / count;
    result.mean_interval_width = interval_widths.mean();
    return result;
}

}
