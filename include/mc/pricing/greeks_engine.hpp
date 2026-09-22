#pragma once

#include "mc/greeks_config.hpp"
#include "mc/greeks_result.hpp"
#include "mc/instruments/instrument.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/simulation_config.hpp"

namespace mc {

class GreeksEngine {
public:
    [[nodiscard]] GreeksResult calculate(const Instrument& instrument,
                                         const MarketData& market,
                                         const OptionParameters& option,
                                         const SimulationConfig& simulation,
                                         const GreeksConfig& config = {}) const;
};

}  // namespace mc
