#include "mc/cli/backtest_options.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "mc/cli/drift_options.hpp"
#include "mc/cli/volatility_options.hpp"

namespace mc::cli {
namespace {

std::string_view take_value(const std::span<const std::string_view> arguments,
                            std::size_t& index, const std::string_view option) {
    if (++index == arguments.size()) {
        throw std::invalid_argument{std::string{option} + " requires a value"};
    }
    return arguments[index];
}

std::size_t parse_positive_size(const std::string_view text,
                                const std::string_view option) {
    std::uint64_t value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
        value > std::numeric_limits<std::size_t>::max()) {
        throw std::invalid_argument{std::string{option} + " requires a positive integer"};
    }
    return static_cast<std::size_t>(value);
}

double parse_positive_double(const std::string_view text,
                             const std::string_view option) {
    double value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument{std::string{option} + " requires a positive number"};
    }
    return value;
}

}

BacktestOptions parse_backtest_options(
    const std::span<const std::string_view> arguments) {
    BacktestOptions options;
    std::vector<std::string_view> seen;
    seen.reserve(arguments.size());

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!argument.starts_with("--")) {
            throw std::invalid_argument{"unexpected positional argument: " +
                                        std::string{argument}};
        }
        if (std::find(seen.begin(), seen.end(), argument) != seen.end()) {
            throw std::invalid_argument{"duplicate option: " + std::string{argument}};
        }
        seen.push_back(argument);

        if (argument == "--csv") {
            options.csv_path = take_value(arguments, index, argument);
        } else if (argument == "--price-column") {
            options.price_column = take_value(arguments, index, argument);
        } else if (argument == "--lookback-days") {
            options.config.lookback_days =
                parse_positive_size(take_value(arguments, index, argument), argument);
        } else if (argument == "--horizon-days") {
            options.config.horizon_days =
                parse_positive_size(take_value(arguments, index, argument), argument);
        } else if (argument == "--step-days") {
            options.config.step_days =
                parse_positive_size(take_value(arguments, index, argument), argument);
        } else if (argument == "--trading-days") {
            options.config.trading_days_per_year =
                parse_positive_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--volatility-model") {
            options.config.volatility_estimator = parse_volatility_estimator(
                take_value(arguments, index, argument));
        } else if (argument == "--ewma-decay") {
            options.config.ewma_decay =
                parse_ewma_decay(take_value(arguments, index, argument));
        } else if (argument == "--drift-model") {
            options.config.drift_estimator = parse_drift_estimator(
                take_value(arguments, index, argument));
        } else if (argument == "--drift-shrinkage") {
            options.config.drift_shrinkage = parse_drift_shrinkage(
                take_value(arguments, index, argument));
        } else {
            throw std::invalid_argument{"unknown option: " + std::string{argument}};
        }
    }

    if (options.csv_path.empty()) {
        throw std::invalid_argument{"--csv is required"};
    }
    if (options.price_column.empty()) {
        throw std::invalid_argument{"--price-column must not be empty"};
    }
    if (std::find(seen.begin(), seen.end(), "--ewma-decay") != seen.end() &&
        options.config.volatility_estimator !=
            forecasting::VolatilityEstimator::ewma) {
        throw std::invalid_argument{
            "--ewma-decay requires --volatility-model ewma"};
    }
    if (std::find(seen.begin(), seen.end(), "--drift-shrinkage") != seen.end() &&
        options.config.drift_estimator !=
            forecasting::DriftEstimator::shrinkage) {
        throw std::invalid_argument{
            "--drift-shrinkage requires --drift-model shrinkage"};
    }
    return options;
}

}
