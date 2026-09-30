#include "mc/models/geometric_brownian_motion.hpp"

#include <cmath>
#include <stdexcept>

#include "mc/validation.hpp"

namespace mc {

GeometricBrownianMotion::GeometricBrownianMotion(const MarketData& market,
                                                 const double maturity,
                                                 const std::size_t num_steps) {
    validate(market);
    if (!std::isfinite(maturity)) {
        throw std::invalid_argument{"maturity must be finite"};
    }
    if (maturity < 0.0) {
        throw std::invalid_argument{"maturity must be nonnegative"};
    }
    if (num_steps == 0) {
        throw std::invalid_argument{"steps must be positive"};
    }

    initial_price_ = market.spot;
    time_step_ = maturity / static_cast<double>(num_steps);
    step_drift_ =
        (market.risk_free_rate - 0.5 * market.volatility * market.volatility) * time_step_;
    step_diffusion_ = market.volatility * std::sqrt(time_step_);
    num_steps_ = num_steps;
    discount_factor_ = std::exp(-market.risk_free_rate * maturity);
}

}
