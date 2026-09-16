#include "mc/validation.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mc {
namespace {

void require_finite(const double value, const std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{std::string{name} + " must be finite"};
    }
}

}  // namespace

void validate(const MarketData& market) {
    require_finite(market.spot, "spot");
    require_finite(market.risk_free_rate, "rate");
    require_finite(market.volatility, "volatility");

    if (market.spot <= 0.0) {
        throw std::invalid_argument{"spot must be positive"};
    }
    if (market.volatility < 0.0) {
        throw std::invalid_argument{"volatility must be nonnegative"};
    }
}

void validate(const OptionParameters& option) {
    require_finite(option.strike, "strike");
    require_finite(option.maturity, "maturity");

    if (option.strike <= 0.0) {
        throw std::invalid_argument{"strike must be positive"};
    }
    if (option.maturity < 0.0) {
        throw std::invalid_argument{"maturity must be nonnegative"};
    }
}

void validate(const SimulationConfig& config) {
    if (config.num_paths == 0) {
        throw std::invalid_argument{"paths must be positive"};
    }
    if (config.num_threads == 0) {
        throw std::invalid_argument{"threads must be positive"};
    }
    if (config.antithetic && config.num_paths % 2 != 0) {
        throw std::invalid_argument{"antithetic simulation requires an even number of paths"};
    }
}

}  // namespace mc
