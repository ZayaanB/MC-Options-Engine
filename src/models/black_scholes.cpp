#include "mc/models/black_scholes.hpp"

#include <cmath>

namespace mc {

BlackScholesModel::BlackScholesModel(const MarketData& market, const double maturity) noexcept
    : spot_{market.spot},
      drift_{(market.risk_free_rate - 0.5 * market.volatility * market.volatility) * maturity},
      diffusion_{market.volatility * std::sqrt(maturity)},
      discount_factor_{std::exp(-market.risk_free_rate * maturity)} {}

}
