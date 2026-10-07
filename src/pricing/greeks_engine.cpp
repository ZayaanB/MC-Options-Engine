#include "mc/pricing/greeks_engine.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/validation.hpp"

namespace mc {
namespace {

constexpr double kVegaPercentagePointScale = 0.01;

void validate_bump(const double bump, const char* const name) {
    if (!std::isfinite(bump) || bump <= 0.0) {
        throw std::invalid_argument{std::string{name} + " must be finite and positive"};
    }
}

}

GreeksResult GreeksEngine::calculate(const Instrument& instrument, const MarketData& market,
                                     const OptionParameters& option,
                                     const SimulationConfig& simulation,
                                     const GreeksConfig& config) const {
    validate(market);
    validate(option);
    validate(simulation);

    const double spot_bump = config.spot_bump.value_or(0.01 * market.spot);
    const double volatility_bump = config.volatility_bump;
    validate_bump(spot_bump, "spot bump");
    validate_bump(volatility_bump, "volatility bump");

    if (spot_bump >= market.spot) {
        throw std::invalid_argument{"spot bump must be smaller than spot"};
    }
    if (market.spot + spot_bump == market.spot ||
        market.spot - spot_bump == market.spot ||
        spot_bump * spot_bump == 0.0 ||
        market.volatility + volatility_bump == market.volatility) {
        throw std::invalid_argument{"Greek bumps must be numerically resolvable"};
    }
    const bool forward_vega = volatility_bump > market.volatility;

    const MarketData spot_up{market.spot + spot_bump, market.risk_free_rate,
                             market.volatility};
    const MarketData spot_down{market.spot - spot_bump, market.risk_free_rate,
                               market.volatility};
    const MarketData volatility_up{market.spot, market.risk_free_rate,
                                   market.volatility + volatility_bump};
    const MarketData volatility_down{market.spot, market.risk_free_rate,
                                     forward_vega ? market.volatility + 2.0 * volatility_bump
                                                  : market.volatility - volatility_bump};

    const MonteCarloEngine pricing_engine;
    const double price = pricing_engine.price(instrument, market, option, simulation).price;
    const double price_spot_up =
        pricing_engine.price(instrument, spot_up, option, simulation).price;
    const double price_spot_down =
        pricing_engine.price(instrument, spot_down, option, simulation).price;
    const double price_volatility_up =
        pricing_engine.price(instrument, volatility_up, option, simulation).price;
    const double price_volatility_down =
        pricing_engine.price(instrument, volatility_down, option, simulation).price;

    const GreeksResult result{
        .delta = (price_spot_up - price_spot_down) / (2.0 * spot_bump),
        .gamma = (price_spot_up - 2.0 * price + price_spot_down) /
                 (spot_bump * spot_bump),
        .vega = ((forward_vega ? -3.0 * price + 4.0 * price_volatility_up - price_volatility_down
                              : price_volatility_up - price_volatility_down) /
                 (2.0 * volatility_bump)) *
                kVegaPercentagePointScale,
    };
    if (!std::isfinite(result.delta) || !std::isfinite(result.gamma) ||
        !std::isfinite(result.vega)) {
        throw std::overflow_error{"Greek calculation exceeds the finite numeric range"};
    }
    return result;
}

}
