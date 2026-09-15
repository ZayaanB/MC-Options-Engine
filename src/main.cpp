#include <iomanip>
#include <iostream>

#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

int main() {
    constexpr mc::MarketData market{
        .spot = 100.0,
        .risk_free_rate = 0.05,
        .volatility = 0.20,
    };
    constexpr mc::OptionParameters option{
        .strike = 100.0,
        .maturity = 1.0,
    };
    constexpr mc::SimulationConfig config{
        .num_paths = 1'000'000,
        .seed = 42,
        .num_threads = 1,
        .batch_size = 0,
        .antithetic = false,
    };

    const mc::EuropeanCall call{option.strike};
    const mc::MonteCarloEngine engine;
    const mc::PricingResult result = engine.price(call, market, option, config);
    const double analytical_price = mc::black_scholes_call(market, option);

    std::cout << std::fixed << std::setprecision(6);
    std::cout << "Monte Carlo Options Pricing Engine\n"
              << "==================================\n\n"
              << "European call (S=100, K=100, r=5%, sigma=20%, T=1)\n"
              << "Analytical price:       " << analytical_price << '\n'
              << "Monte Carlo price:      " << result.price << '\n'
              << "Standard error:         " << result.standard_error << '\n'
              << "95% confidence interval:[" << result.confidence_lower << ", "
              << result.confidence_upper << "]\n"
              << "Paths:                  " << result.paths << '\n'
              << "Runtime:                " << result.runtime_seconds << " s\n"
              << "Throughput:             " << result.paths_per_second / 1'000'000.0
              << "M paths/s\n";

    return 0;
}
