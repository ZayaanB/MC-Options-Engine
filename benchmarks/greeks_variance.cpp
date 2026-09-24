#include <chrono>
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
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"
#include "mc/statistics/running_statistics.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::uint64_t kPathsPerPrice = 50'000;
constexpr std::uint64_t kReplications = 100;
constexpr double kSpotBump = 1.0;
constexpr std::uint64_t kSeedOffset = 0x9e3779b97f4a7c15ULL;

struct ExperimentResult {
    double mean_delta{};
    double variance{};
    double standard_deviation{};
    double runtime_seconds{};
};

template <typename DeltaEstimator>
ExperimentResult run_experiment(DeltaEstimator&& estimate_delta) {
    mc::RunningStatistics deltas;
    const auto start = std::chrono::steady_clock::now();
    for (std::uint64_t replication = 0; replication < kReplications; ++replication) {
        deltas.add(estimate_delta(42 + replication));
    }
    const auto stop = std::chrono::steady_clock::now();
    return {
        .mean_delta = deltas.mean(),
        .variance = deltas.variance(),
        .standard_deviation = std::sqrt(deltas.variance()),
        .runtime_seconds = std::chrono::duration<double>(stop - start).count(),
    };
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument{"usage: mc_greeks_variance [output.csv]"};
        }
        const std::filesystem::path output_path =
            argc == 2 ? std::filesystem::path{argv[1]}
                      : std::filesystem::path{"results/greeks_variance.csv"};
        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine pricing_engine;

        const auto common = run_experiment([&](const std::uint64_t seed) {
            const mc::MarketData up{kMarket.spot + kSpotBump, kMarket.risk_free_rate,
                                    kMarket.volatility};
            const mc::MarketData down{kMarket.spot - kSpotBump, kMarket.risk_free_rate,
                                      kMarket.volatility};
            const mc::SimulationConfig simulation{kPathsPerPrice, seed, 1, 0, false};
            const double up_price = pricing_engine.price(call, up, kOption, simulation).price;
            const double down_price = pricing_engine.price(call, down, kOption, simulation).price;
            return (up_price - down_price) / (2.0 * kSpotBump);
        });

        const auto independent = run_experiment([&](const std::uint64_t seed) {
            const mc::MarketData up{kMarket.spot + kSpotBump, kMarket.risk_free_rate,
                                    kMarket.volatility};
            const mc::MarketData down{kMarket.spot - kSpotBump, kMarket.risk_free_rate,
                                      kMarket.volatility};
            const mc::SimulationConfig up_config{kPathsPerPrice, seed, 1, 0, false};
            const mc::SimulationConfig down_config{kPathsPerPrice, seed + kSeedOffset, 1, 0,
                                                   false};
            const double up_price = pricing_engine.price(call, up, kOption, up_config).price;
            const double down_price = pricing_engine.price(call, down, kOption, down_config).price;
            return (up_price - down_price) / (2.0 * kSpotBump);
        });

        if (!(common.variance > 0.0) || !(independent.variance > 0.0)) {
            throw std::runtime_error{"Delta experiment produced invalid variance"};
        }
        const double variance_ratio = independent.variance / common.variance;
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open benchmark output: " + output_path.string()};
        }
        output << "mode,replications,paths_per_price,spot_bump,mean_delta,sample_variance,"
                  "standard_deviation,total_runtime_seconds,variance_ratio_vs_common\n"
               << std::setprecision(17);
        output << "common," << kReplications << ',' << kPathsPerPrice << ',' << kSpotBump << ','
               << common.mean_delta << ',' << common.variance << ',' << common.standard_deviation
               << ',' << common.runtime_seconds << ",1\n";
        output << "independent," << kReplications << ',' << kPathsPerPrice << ',' << kSpotBump
               << ',' << independent.mean_delta << ',' << independent.variance << ','
               << independent.standard_deviation << ',' << independent.runtime_seconds << ','
               << variance_ratio << '\n';
        if (!output) {
            throw std::runtime_error{"failed while writing Greek variance benchmark output"};
        }

        std::cout << std::fixed << std::setprecision(8)
                  << "Delta estimator variance across " << kReplications << " replications\n"
                  << "Common random numbers:      " << common.variance << '\n'
                  << "Independent random numbers: " << independent.variance << '\n'
                  << "Variance reduction:         " << variance_ratio << "x\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Greek variance benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
