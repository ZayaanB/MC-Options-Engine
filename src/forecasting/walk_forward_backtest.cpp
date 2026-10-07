#include "mc/forecasting/walk_forward_backtest.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>

#include "mc/forecasting/historical_gbm.hpp"
#include "mc/statistics/running_statistics.hpp"

namespace mc::forecasting {
namespace {

constexpr double kNormal95 = 1.959963984540054;

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
        const double nan = std::numeric_limits<double>::quiet_NaN();
        if (directional_predictions_ == 0) {
            return ForecastErrorMetrics{
                absolute_errors_.mean(), std::sqrt(squared_errors_.mean()),
                percentage_errors_.mean(), 0, 0, nan, nan, nan};
        }
        const double count = static_cast<double>(directional_predictions_);
        const double directional_accuracy =
            static_cast<double>(correct_directions_) / count;
        const double squared_z = kNormal95 * kNormal95;
        const double denominator = 1.0 + squared_z / count;
        const double center =
            (directional_accuracy + squared_z / (2.0 * count)) / denominator;
        const double margin =
            kNormal95 / denominator *
            std::sqrt(directional_accuracy * (1.0 - directional_accuracy) /
                          count +
                      squared_z / (4.0 * count * count));
        return ForecastErrorMetrics{
            absolute_errors_.mean(), std::sqrt(squared_errors_.mean()),
            percentage_errors_.mean(), directional_predictions_,
            correct_directions_, directional_accuracy,
            std::max(0.0, center - margin), std::min(1.0, center + margin)};
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

double percentile(const std::span<const double> sorted_values,
                  const double probability) {
    const double position =
        probability * static_cast<double>(sorted_values.size() - 1);
    const auto lower_index = static_cast<std::size_t>(std::floor(position));
    const auto upper_index = static_cast<std::size_t>(std::ceil(position));
    const double fraction = position - static_cast<double>(lower_index);
    return sorted_values[lower_index] +
           fraction * (sorted_values[upper_index] - sorted_values[lower_index]);
}

MaeImprovementEstimate bootstrap_mae_improvement(
    const std::span<const BacktestPoint> points, const BacktestConfig& config) {
    std::vector<double> paired_improvements;
    paired_improvements.reserve(points.size());
    double estimate = 0.0;
    for (const auto& point : points) {
        const double improvement =
            std::abs(point.latest_price_forecast - point.actual_price) -
            std::abs(point.forecast_price - point.actual_price);
        paired_improvements.push_back(improvement);
        estimate += (improvement - estimate) /
                    static_cast<double>(paired_improvements.size());
    }

    const std::size_t block_length = std::min(
        points.size(), 1 + (config.horizon_days - 1) / config.step_days);
    std::mt19937_64 generator{config.bootstrap_seed};
    std::uniform_int_distribution<std::size_t> start_distribution{
        0, paired_improvements.size() - 1};
    std::vector<double> bootstrap_estimates;
    bootstrap_estimates.reserve(config.bootstrap_samples);
    for (std::size_t sample = 0; sample < config.bootstrap_samples; ++sample) {
        double sample_mean = 0.0;
        std::size_t drawn = 0;
        while (drawn < paired_improvements.size()) {
            const std::size_t start = start_distribution(generator);
            const std::size_t take =
                std::min(block_length, paired_improvements.size() - drawn);
            for (std::size_t offset = 0; offset < take; ++offset) {
                const double value = paired_improvements[
                    (start + offset) % paired_improvements.size()];
                sample_mean +=
                    (value - sample_mean) / static_cast<double>(drawn + offset + 1);
            }
            drawn += take;
        }
        bootstrap_estimates.push_back(sample_mean);
    }
    std::sort(bootstrap_estimates.begin(), bootstrap_estimates.end());
    const double lower = percentile(bootstrap_estimates, 0.025);
    const double upper = percentile(bootstrap_estimates, 0.975);
    ComparisonConclusion conclusion = ComparisonConclusion::inconclusive;
    if (lower > 0.0) {
        conclusion = ComparisonConclusion::better;
    } else if (upper < 0.0) {
        conclusion = ComparisonConclusion::worse;
    }
    const double baseline_mae = [&points] {
        double mean = 0.0;
        std::size_t count = 0;
        for (const auto& point : points) {
            ++count;
            const double error =
                std::abs(point.latest_price_forecast - point.actual_price);
            mean += (error - mean) / static_cast<double>(count);
        }
        return mean;
    }();
    const double relative =
        baseline_mae == 0.0
            ? std::numeric_limits<double>::quiet_NaN()
            : estimate / baseline_mae;
    return MaeImprovementEstimate{estimate,
                                  relative,
                                  lower,
                                  upper,
                                  config.bootstrap_samples,
                                  config.bootstrap_seed,
                                  block_length,
                                  conclusion};
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
    if (config.bootstrap_samples < 100) {
        throw std::invalid_argument{
            "backtest bootstrap samples must be at least 100"};
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
    result.mae_improvement = bootstrap_mae_improvement(result.points, config);
    result.interval_coverage = static_cast<double>(covered_intervals) / count;
    result.mean_interval_width = interval_widths.mean();
    return result;
}

}
