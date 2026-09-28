#pragma once

#include "mc/instruments/arithmetic_asian_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing_result.hpp"
#include "mc/simulation_config.hpp"

namespace mc {

class PathMonteCarloEngine {
public:
    [[nodiscard]] PricingResult price(const ArithmeticAsianCall& instrument,
                                      const MarketData& market,
                                      const OptionParameters& option,
                                      const SimulationConfig& config) const;
};

}  // namespace mc
