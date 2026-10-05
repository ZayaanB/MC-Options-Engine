#include "mc/cli/drift_options.hpp"

#include <charconv>
#include <cmath>
#include <stdexcept>
#include <system_error>

namespace mc::cli {

forecasting::DriftEstimator parse_drift_estimator(
    const std::string_view text) {
    if (text == "historical") {
        return forecasting::DriftEstimator::historical;
    }
    if (text == "zero") {
        return forecasting::DriftEstimator::zero;
    }
    if (text == "shrinkage") {
        return forecasting::DriftEstimator::shrinkage;
    }
    throw std::invalid_argument{
        "--drift-model must be historical, zero, or shrinkage"};
}

double parse_drift_shrinkage(const std::string_view text) {
    double value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value < 0.0 || value > 1.0) {
        throw std::invalid_argument{
            "--drift-shrinkage requires a number from zero to one"};
    }
    return value;
}

std::string_view drift_estimator_name(
    const forecasting::DriftEstimator estimator) noexcept {
    switch (estimator) {
        case forecasting::DriftEstimator::historical:
            return "historical";
        case forecasting::DriftEstimator::zero:
            return "zero";
        case forecasting::DriftEstimator::shrinkage:
            return "shrinkage";
    }
    return "unknown";
}

}
