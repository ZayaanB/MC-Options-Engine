#include "mc/cli/volatility_options.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>

namespace mc::cli {

forecasting::VolatilityEstimator parse_volatility_estimator(
    const std::string_view text) {
    if (text == "sample") {
        return forecasting::VolatilityEstimator::sample;
    }
    if (text == "ewma") {
        return forecasting::VolatilityEstimator::ewma;
    }
    throw std::invalid_argument{"--volatility-model must be sample or ewma"};
}

double parse_ewma_decay(const std::string_view text) {
    double value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value <= 0.0 || value >= 1.0) {
        throw std::invalid_argument{
            "--ewma-decay requires a number between zero and one"};
    }
    return value;
}

std::string_view volatility_estimator_name(
    const forecasting::VolatilityEstimator estimator) noexcept {
    switch (estimator) {
        case forecasting::VolatilityEstimator::sample:
            return "sample";
        case forecasting::VolatilityEstimator::ewma:
            return "ewma";
    }
    return "unknown";
}

}
