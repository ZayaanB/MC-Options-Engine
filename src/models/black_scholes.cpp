#include "mc/models/black_scholes.hpp"

#include <cmath>

namespace mc {

BlackScholesModel::BlackScholesModel(const MarketData& market, const double maturity) noexcept
    : spot_{market.spot},
      drift_{(market.risk_free_rate - 0.5 * market.volatility * market.volatility) * maturity},
      diffusion_{market.volatility * std::sqrt(maturity)},
      discount_factor_{std::exp(-market.risk_free_rate * maturity)} {}

double BlackScholesModel::terminal_price(const double standard_normal) const noexcept {
    return spot_ * std::exp(drift_ + diffusion_ * standard_normal);
}

double BlackScholesModel::discount_factor() const noexcept { return discount_factor_; }

}  // namespace mc
