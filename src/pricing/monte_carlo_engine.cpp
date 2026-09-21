#include "mc/pricing/monte_carlo_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <random>
#include <thread>
#include <vector>

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
}

// Preserve the original stream for one-thread runs; mix additional worker seeds.
std::uint64_t worker_seed(const std::uint64_t seed, const std::size_t worker_id) noexcept {
    if (worker_id == 0) {
        return seed;
    }
    std::uint64_t value = seed + static_cast<std::uint64_t>(worker_id) *
                                    0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31);
}

RunningStatistics simulate_worker(const Instrument& instrument, const BlackScholesModel& model,
                                  const std::uint64_t observations, const std::uint64_t seed,
                                  const bool antithetic) {
    std::mt19937_64 random_engine{seed};
    std::normal_distribution<double> standard_normal{0.0, 1.0};
    RunningStatistics statistics;

    for (std::uint64_t observation = 0; observation < observations; ++observation) {
        const double normal = standard_normal(random_engine);
        const double payoff = instrument.payoff(model.terminal_price(normal));
        if (antithetic) {
            const double opposite_payoff = instrument.payoff(model.terminal_price(-normal));
            statistics.add(model.discount_factor() * 0.5 * (payoff + opposite_payoff));
        } else {
            statistics.add(model.discount_factor() * payoff);
        }
    }
    return statistics;
}

}  // namespace

PricingResult MonteCarloEngine::price(const Instrument& instrument, const MarketData& market,
                                      const OptionParameters& option,
                                      const SimulationConfig& config) const {
    validate_inputs(market, option, config);

    const BlackScholesModel model{market, option.maturity};
    RunningStatistics statistics;
    const std::uint64_t observations =
        config.antithetic ? config.num_paths / 2 : config.num_paths;

    const auto start = std::chrono::steady_clock::now();

    if (config.num_threads == 1) {
        statistics = simulate_worker(instrument, model, observations, config.seed,
                                     config.antithetic);
    } else {
        // An antithetic pair is indivisible and is assigned to one worker.
        const auto worker_count = static_cast<std::size_t>(
            std::min<std::uint64_t>(observations, config.num_threads));
        const auto base_observations = observations / worker_count;
        const auto remainder = observations % worker_count;
        std::vector<RunningStatistics> worker_statistics(worker_count);
        std::vector<std::exception_ptr> worker_errors(worker_count);
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);

        for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
            const auto worker_observations = base_observations + (worker_id < remainder ? 1 : 0);
            workers.emplace_back([&, worker_id, worker_observations] {
                try {
                    worker_statistics[worker_id] = simulate_worker(
                        instrument, model, worker_observations,
                        worker_seed(config.seed, worker_id), config.antithetic);
                } catch (...) {
                    worker_errors[worker_id] = std::current_exception();
                }
            });
        }

        for (auto& worker : workers) {
            worker.join();
        }
        for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
            if (worker_errors[worker_id]) {
                std::rethrow_exception(worker_errors[worker_id]);
            }
            statistics.merge(worker_statistics[worker_id]);
        }
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
        .observations = statistics.count(),
        .runtime_seconds = runtime_seconds,
        .paths_per_second = static_cast<double>(config.num_paths) / runtime_seconds,
    };
}

}  // namespace mc
