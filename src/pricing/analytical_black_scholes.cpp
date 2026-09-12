#include "mc/pricing/analytical_black_scholes.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mc {
namespace {

struct AnalyticalPrices {
    double call;
    double put;
};

void require_finite(const double value, const std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{std::string{name} + " must be finite"};
    }
}

void validate_inputs(const double spot, const double strike, const double rate,
                     const double volatility, const double maturity) {
    require_finite(spot, "spot");
    require_finite(strike, "strike");
    require_finite(rate, "rate");
    require_finite(volatility, "volatility");
    require_finite(maturity, "maturity");

    if (spot <= 0.0) {
        throw std::invalid_argument{"spot must be positive"};
    }
    if (strike <= 0.0) {
        throw std::invalid_argument{"strike must be positive"};
    }
    if (volatility < 0.0) {
        throw std::invalid_argument{"volatility must be nonnegative"};
    }
    if (maturity < 0.0) {
        throw std::invalid_argument{"maturity must be nonnegative"};
    }
}

AnalyticalPrices calculate_prices(const double spot, const double strike, const double rate,
                                  const double volatility, const double maturity) {
    validate_inputs(spot, strike, rate, volatility, maturity);

    if (maturity == 0.0) {
        return {
            .call = std::max(spot - strike, 0.0),
            .put = std::max(strike - spot, 0.0),
        };
    }

    const double discount_factor = std::exp(-rate * maturity);
    const double discounted_strike = strike * discount_factor;

    if (volatility == 0.0) {
        return {
            .call = std::max(spot - discounted_strike, 0.0),
            .put = std::max(discounted_strike - spot, 0.0),
        };
    }

    const double volatility_time = volatility * std::sqrt(maturity);
    const double d1 =
        (std::log(spot) - std::log(strike) +
         (rate + 0.5 * volatility * volatility) * maturity) /
        volatility_time;
    const double d2 = d1 - volatility_time;

    return {
        .call = spot * standard_normal_cdf(d1) - discounted_strike * standard_normal_cdf(d2),
        .put = discounted_strike * standard_normal_cdf(-d2) -
               spot * standard_normal_cdf(-d1),
    };
}

}  // namespace

double standard_normal_cdf(const double value) noexcept {
    return 0.5 * std::erfc(-value / std::sqrt(2.0));
}

double black_scholes_call(const double spot, const double strike, const double rate,
                          const double volatility, const double maturity) {
    return calculate_prices(spot, strike, rate, volatility, maturity).call;
}

double black_scholes_put(const double spot, const double strike, const double rate,
                         const double volatility, const double maturity) {
    return calculate_prices(spot, strike, rate, volatility, maturity).put;
}

double black_scholes_call(const MarketData& market, const OptionParameters& option) {
    return black_scholes_call(market.spot, option.strike, market.risk_free_rate,
                              market.volatility, option.maturity);
}

double black_scholes_put(const MarketData& market, const OptionParameters& option) {
    return black_scholes_put(market.spot, option.strike, market.risk_free_rate,
                             market.volatility, option.maturity);
}

}  // namespace mc
