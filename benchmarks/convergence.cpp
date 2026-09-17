#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>

#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{
    .spot = 100.0,
    .risk_free_rate = 0.05,
    .volatility = 0.20,
};

constexpr mc::OptionParameters kOption{
    .strike = 100.0,
    .maturity = 1.0,
};

constexpr std::array<std::uint64_t, 5> kPathCounts{
    1'000,
    10'000,
    100'000,
    1'000'000,
    5'000'000,
};

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        const std::filesystem::path output_path =
            argc > 1 ? std::filesystem::path{argv[1]}
                     : std::filesystem::path{"results/convergence.csv"};
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }

        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open convergence output: " +
                                     output_path.string()};
        }

        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine engine;
        const double analytical_price = mc::black_scholes_call(kMarket, kOption);

        output << "paths,mc_price,analytical_price,absolute_error,standard_error,runtime_seconds\n";
        output << std::setprecision(17);

        std::cout << "Monte Carlo convergence experiment\n"
                  << "Output: " << output_path << "\n\n";
        std::cout << std::fixed << std::setprecision(6);

        for (const std::uint64_t paths : kPathCounts) {
            const mc::SimulationConfig config{
                .num_paths = paths,
                .seed = 42,
                .num_threads = 1,
                .batch_size = 0,
                .antithetic = false,
            };
            const mc::PricingResult result = engine.price(call, kMarket, kOption, config);
            const double absolute_error = std::abs(result.price - analytical_price);

            output << paths << ',' << result.price << ',' << analytical_price << ','
                   << absolute_error << ',' << result.standard_error << ','
                   << result.runtime_seconds << '\n';

            std::cout << std::setw(10) << paths << " paths  price=" << result.price
                      << "  error=" << absolute_error << "  SE=" << result.standard_error
                      << "  runtime=" << result.runtime_seconds << " s\n";
        }

        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Convergence experiment failed: " << error.what() << '\n';
        return 1;
    }
}
