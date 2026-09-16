#pragma once

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/simulation_config.hpp"

namespace mc {

void validate(const MarketData& market);
void validate(const OptionParameters& option);
void validate(const SimulationConfig& config);

}  // namespace mc
