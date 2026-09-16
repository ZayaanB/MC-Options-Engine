#include "mc/pricing/monte_carlo_engine.hpp"

#include <chrono>
#include <random>
#include <stdexcept>

#include "mc/models/black_scholes.hpp"
#include "mc/statistics/running_statistics.hpp"
#include "mc/validation.hpp"

namespace mc {
namespace {

constexpr double kConfidenceMultiplier95 = 1.96;

void validate_inputs(const MarketData& market, const OptionParameters& option,
                     const SimulationConfig& config) {
    validate(market);
    validate(option);
    validate(config);

    if (config.num_threads != 1) {
        throw std::invalid_argument{"single-threaded simulation requires exactly one thread"};
    }
    if (config.antithetic) {
        throw std::invalid_argument{"antithetic simulation is not available yet"};
    }
}

}  // namespace

PricingResult MonteCarloEngine::price(const Instrument& instrument, const MarketData& market,
                                      const OptionParameters& option,
                                      const SimulationConfig& config) const {
    validate_inputs(market, option, config);

    const BlackScholesModel model{market, option.maturity};
    std::mt19937_64 random_engine{config.seed};
    std::normal_distribution<double> standard_normal{0.0, 1.0};
    RunningStatistics statistics;

    const auto start = std::chrono::steady_clock::now();

    for (std::uint64_t path = 0; path < config.num_paths; ++path) {
        const double terminal_price = model.terminal_price(standard_normal(random_engine));
        const double discounted_payoff = model.discount_factor() * instrument.payoff(terminal_price);
        statistics.add(discounted_payoff);
    }

    const auto stop = std::chrono::steady_clock::now();
    const double runtime_seconds = std::chrono::duration<double>(stop - start).count();
    const double standard_error = statistics.standard_error();

    return {
        .price = statistics.mean(),
        .sample_variance = statistics.variance(),
        .standard_error = standard_error,
        .confidence_lower = statistics.mean() - kConfidenceMultiplier95 * standard_error,
        .confidence_upper = statistics.mean() + kConfidenceMultiplier95 * standard_error,
        .paths = config.num_paths,
        .runtime_seconds = runtime_seconds,
        .paths_per_second = static_cast<double>(config.num_paths) / runtime_seconds,
    };
}

}  // namespace mc
