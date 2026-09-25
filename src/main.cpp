#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mc/cli/price_options.hpp"
#include "mc/instruments/european_call.hpp"
#include "mc/instruments/european_put.hpp"
#include "mc/instruments/instrument.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/validation.hpp"

namespace {

constexpr std::string_view kUsage = R"(Usage:
  mcprice price [options]

Options:
  --type call|put               Instrument type (default: call)
  --method mc|analytical|both   Pricing method (default: both)
  --spot VALUE                  Spot price (default: 100)
  --strike VALUE                Strike price (default: 100)
  --rate VALUE                  Continuously compounded rate (default: 0.05)
  --volatility VALUE            Annualized volatility (default: 0.20)
  --maturity VALUE              Maturity in years (default: 1)
  --paths N                     Total Monte Carlo trajectories (default: 1000000)
  --threads N                   Worker threads (default: 1)
  --seed N                      Unsigned RNG seed (default: 42)
  --antithetic                  Enable antithetic variates; paths must be even
  --help                        Show this help
)";

double analytical_price(const mc::cli::OptionType type, const mc::MarketData& market,
                        const mc::OptionParameters& option) {
    return type == mc::cli::OptionType::call ? mc::black_scholes_call(market, option)
                                             : mc::black_scholes_put(market, option);
}

std::unique_ptr<mc::Instrument> make_instrument(const mc::cli::OptionType type,
                                                const double strike) {
    if (type == mc::cli::OptionType::call) {
        return std::make_unique<mc::EuropeanCall>(strike);
    }
    return std::make_unique<mc::EuropeanPut>(strike);
}

void print_inputs(const mc::cli::PriceOptions& options) {
    std::cout << "European " << mc::cli::option_type_name(options.type) << '\n'
              << "Method:                 " << mc::cli::pricing_method_name(options.method)
              << '\n'
              << "Spot:                   " << options.market.spot << '\n'
              << "Strike:                 " << options.option.strike << '\n'
              << "Rate:                   " << options.market.risk_free_rate << '\n'
              << "Volatility:             " << options.market.volatility << '\n'
              << "Maturity:               " << options.option.maturity << " years\n";
}

void run_price(const mc::cli::PriceOptions& options) {
    mc::validate(options.market);
    mc::validate(options.option);

    const bool run_analytical = options.method != mc::cli::PricingMethod::monte_carlo;
    const bool run_monte_carlo = options.method != mc::cli::PricingMethod::analytical;
    if (run_monte_carlo) {
        mc::validate(options.simulation);
    }
    const double reference =
        run_analytical ? analytical_price(options.type, options.market, options.option) : 0.0;

    std::cout << std::fixed << std::setprecision(6)
              << "Monte Carlo Options Pricing Engine\n"
              << "==================================\n\n";
    print_inputs(options);
    std::cout << '\n';

    if (run_analytical) {
        std::cout << "Analytical price:       " << reference << '\n';
    }
    if (run_monte_carlo) {
        const auto instrument = make_instrument(options.type, options.option.strike);
        const mc::MonteCarloEngine engine;
        const auto result =
            engine.price(*instrument, options.market, options.option, options.simulation);

        std::cout << "Monte Carlo estimate:   " << result.price << '\n';
        if (run_analytical) {
            std::cout << "Absolute difference:    " << std::abs(result.price - reference) << '\n';
        }
        std::cout << "Standard error:         " << result.standard_error << '\n'
                  << "95% confidence interval:[" << result.confidence_lower << ", "
                  << result.confidence_upper << "]\n"
                  << "Paths:                  " << result.paths << '\n'
                  << "Observations:           " << result.observations << '\n'
                  << "Threads:                " << options.simulation.num_threads << '\n'
                  << "Seed:                   " << options.simulation.seed << '\n'
                  << "Antithetic:             "
                  << (options.simulation.antithetic ? "yes" : "no") << '\n'
                  << "Runtime:                " << result.runtime_seconds << " s\n"
                  << "Throughput:             "
                  << result.paths_per_second / 1'000'000.0 << " M paths/s\n";
    }
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        if (arguments.empty() || arguments.front() == "--help" ||
            arguments.front() == "-h") {
            std::cout << kUsage;
            return 0;
        }
        if (arguments.front() != "price") {
            throw std::invalid_argument{"unknown command: " + std::string{arguments.front()}};
        }
        arguments.erase(arguments.begin());
        if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
            std::cout << kUsage;
            return 0;
        }

        run_price(mc::cli::parse_price_options(arguments));
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n" << kUsage;
        return 1;
    }
}
