#include "mc/pricing/path_monte_carlo_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <exception>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

#include "mc/models/geometric_brownian_motion.hpp"
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
    if (config.num_steps == 0) {
        throw std::invalid_argument{"steps must be positive for path simulation"};
    }
}

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

RunningStatistics simulate_batch(const ArithmeticAsianCall& instrument,
                                 const GeometricBrownianMotion& model,
                                 const std::uint64_t observations,
                                 std::mt19937_64& random_engine,
                                 std::normal_distribution<double>& standard_normal,
                                 const bool antithetic) {
    RunningStatistics statistics;

    for (std::uint64_t observation = 0; observation < observations; ++observation) {
        double price = model.initial_price();
        double price_sum = 0.0;

        if (antithetic) {
            double opposite_price = model.initial_price();
            double opposite_price_sum = 0.0;
            for (std::size_t step = 0; step < model.num_steps(); ++step) {
                const double normal = standard_normal(random_engine);
                price = model.advance(price, normal);
                opposite_price = model.advance(opposite_price, -normal);
                price_sum += price;
                opposite_price_sum += opposite_price;
            }
            const double denominator = static_cast<double>(model.num_steps());
            const double paired_payoff =
                0.5 * (instrument.payoff(price_sum / denominator) +
                       instrument.payoff(opposite_price_sum / denominator));
            statistics.add(model.discount_factor() * paired_payoff);
        } else {
            for (std::size_t step = 0; step < model.num_steps(); ++step) {
                price = model.advance(price, standard_normal(random_engine));
                price_sum += price;
            }
            const double average = price_sum / static_cast<double>(model.num_steps());
            statistics.add(model.discount_factor() * instrument.payoff(average));
        }
    }
    return statistics;
}

RunningStatistics simulate_worker(const ArithmeticAsianCall& instrument,
                                  const GeometricBrownianMotion& model,
                                  const std::uint64_t observations,
                                  const std::uint64_t seed,
                                  const std::size_t configured_batch_size,
                                  const bool antithetic) {
    std::mt19937_64 random_engine{seed};
    std::normal_distribution<double> standard_normal{0.0, 1.0};
    RunningStatistics statistics;
    const std::uint64_t batch_size = configured_batch_size == 0
                                         ? observations
                                         : static_cast<std::uint64_t>(configured_batch_size);

    std::uint64_t remaining = observations;
    while (remaining > 0) {
        const std::uint64_t current_batch = std::min(remaining, batch_size);
        statistics.merge(simulate_batch(instrument, model, current_batch, random_engine,
                                        standard_normal, antithetic));
        remaining -= current_batch;
    }
    return statistics;
}

}  // namespace

PricingResult PathMonteCarloEngine::price(const ArithmeticAsianCall& instrument,
                                          const MarketData& market,
                                          const OptionParameters& option,
                                          const SimulationConfig& config) const {
    validate_inputs(market, option, config);
    const GeometricBrownianMotion model{market, option.maturity, config.num_steps};
    const std::uint64_t observations =
        config.antithetic ? config.num_paths / 2 : config.num_paths;
    RunningStatistics statistics;
    const auto start = std::chrono::steady_clock::now();

    if (config.num_threads == 1) {
        statistics =
            simulate_worker(instrument, model, observations, config.seed, config.batch_size,
                            config.antithetic);
    } else {
        const auto worker_count = static_cast<std::size_t>(
            std::min<std::uint64_t>(observations, config.num_threads));
        const auto base_observations = observations / worker_count;
        const auto remainder = observations % worker_count;
        std::vector<RunningStatistics> worker_statistics(worker_count);
        std::vector<std::exception_ptr> worker_errors(worker_count);
        std::vector<std::jthread> workers;
        workers.reserve(worker_count);

        for (std::size_t worker_id = 0; worker_id < worker_count; ++worker_id) {
            const auto worker_observations =
                base_observations + (worker_id < remainder ? 1 : 0);
            workers.emplace_back([&, worker_id, worker_observations] {
                try {
                    worker_statistics[worker_id] =
                        simulate_worker(instrument, model, worker_observations,
                                        worker_seed(config.seed, worker_id), config.batch_size,
                                        config.antithetic);
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
