#include "mc/models/black_scholes.hpp"

#include <cmath>
#include <stdexcept>

#include "mc/validation.hpp"

namespace mc {

BlackScholesModel::BlackScholesModel(const MarketData& market,
                                     const double maturity) {
    validate(market);
    if (!std::isfinite(maturity)) {
        throw std::invalid_argument{"maturity must be finite"};
    }
    if (maturity < 0.0) {
        throw std::invalid_argument{"maturity must be nonnegative"};
    }

    spot_ = market.spot;
    if (maturity == 0.0) {
        discount_factor_ = 1.0;
        return;
    }
    drift_ = (market.risk_free_rate -
              0.5 * market.volatility * market.volatility) *
             maturity;
    diffusion_ = market.volatility * std::sqrt(maturity);
    discount_factor_ = std::exp(-market.risk_free_rate * maturity);
    if (!std::isfinite(drift_) || !std::isfinite(diffusion_) ||
        !std::isfinite(discount_factor_)) {
        throw std::overflow_error{"Black-Scholes parameters exceed the finite numeric range"};
    }
}

}
