#pragma once

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"

namespace mc {

[[nodiscard]] double standard_normal_cdf(double value) noexcept;

[[nodiscard]] double black_scholes_call(double spot, double strike, double rate,
                                        double volatility, double maturity);

[[nodiscard]] double black_scholes_put(double spot, double strike, double rate,
                                       double volatility, double maturity);

[[nodiscard]] double black_scholes_call(const MarketData& market,
                                        const OptionParameters& option);

[[nodiscard]] double black_scholes_put(const MarketData& market,
                                       const OptionParameters& option);

}  // namespace mc
