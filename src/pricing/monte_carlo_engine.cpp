#include "mc/pricing/monte_carlo_engine.hpp"

#include <chrono>
#include <cmath>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include "mc/models/black_scholes.hpp"
#include "mc/statistics/running_statistics.hpp"

namespace mc {
namespace {

constexpr double kConfidenceMultiplier95 = 1.96;

void require_finite(const double value, const std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument{std::string{name} + " must be finite"};
    }
}

void validate_inputs(const MarketData& market, const OptionParameters& option,
                     const SimulationConfig& config) {
    require_finite(market.spot, "spot");
    require_finite(option.strike, "strike");
    require_finite(market.risk_free_rate, "rate");
    require_finite(market.volatility, "volatility");
    require_finite(option.maturity, "maturity");

    if (market.spot <= 0.0) {
        throw std::invalid_argument{"spot must be positive"};
    }
    if (option.strike <= 0.0) {
        throw std::invalid_argument{"strike must be positive"};
    }
    if (market.volatility < 0.0) {
        throw std::invalid_argument{"volatility must be nonnegative"};
    }
    if (option.maturity < 0.0) {
        throw std::invalid_argument{"maturity must be nonnegative"};
    }
    if (config.num_paths == 0) {
        throw std::invalid_argument{"paths must be positive"};
    }
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
