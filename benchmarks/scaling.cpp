#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>

#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::array<std::uint64_t, 3> kPathCounts{1'000'000, 5'000'000, 10'000'000};
constexpr std::array<std::size_t, 4> kThreadCounts{1, 2, 4, 8};
constexpr std::size_t kRepetitions = 3;

struct Measurement {
    std::array<double, kRepetitions> runtimes{};
    double median_runtime{};
    double price{};
    double standard_error{};
};

Measurement measure(const mc::MonteCarloEngine& engine, const mc::EuropeanCall& call,
                    const std::uint64_t paths, const std::size_t threads) {
    const mc::SimulationConfig config{
        .num_paths = paths,
        .seed = 42,
        .num_threads = threads,
        .batch_size = 0,
        .antithetic = false,
    };
    Measurement measurement;
    double expected_price = 0.0;
    double expected_standard_error = 0.0;

    for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
        const auto result = engine.price(call, kMarket, kOption, config);
        if (result.paths != paths || !std::isfinite(result.price) ||
            !std::isfinite(result.standard_error) || result.runtime_seconds <= 0.0) {
            throw std::runtime_error{"invalid Monte Carlo result during scaling benchmark"};
        }
        if (repetition == 0) {
            expected_price = result.price;
            expected_standard_error = result.standard_error;
        } else if (result.price != expected_price ||
                   result.standard_error != expected_standard_error) {
            throw std::runtime_error{"identical benchmark configuration was not reproducible"};
        }
        measurement.runtimes[repetition] = result.runtime_seconds;
    }

    auto sorted_runtimes = measurement.runtimes;
    std::sort(sorted_runtimes.begin(), sorted_runtimes.end());
    measurement.median_runtime = sorted_runtimes[kRepetitions / 2];
    measurement.price = expected_price;
    measurement.standard_error = expected_standard_error;
    return measurement;
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument{"usage: mc_scaling [output.csv]"};
        }
        const std::filesystem::path output_path =
            argc == 2 ? std::filesystem::path{argv[1]}
                      : std::filesystem::path{"results/scaling.csv"};

        const unsigned int available_threads = std::thread::hardware_concurrency();
        const std::size_t max_threads = available_threads == 0 ? 1 : available_threads;
        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine engine;

        // Warm the code path before collecting timed runs.
        const mc::SimulationConfig warmup{100'000, 42, 1, 0, false};
        (void)engine.price(call, kMarket, kOption, warmup);

        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open scaling output: " + output_path.string()};
        }
        output << "paths,threads,repetitions,runtime_1_seconds,runtime_2_seconds,"
                  "runtime_3_seconds,median_runtime_seconds,throughput_paths_per_second,"
                  "speedup,parallel_efficiency,mc_price,standard_error\n";
        output << std::setprecision(17);

        std::cout << "Monte Carlo thread scaling benchmark\n"
                  << "Hardware concurrency: " << available_threads << " logical threads\n"
                  << "Output: " << output_path << "\n\n"
                  << std::fixed << std::setprecision(3);

        for (const auto paths : kPathCounts) {
            double baseline_runtime = 0.0;
            for (const auto threads : kThreadCounts) {
                if (threads > max_threads) {
                    continue;
                }
                const auto measurement = measure(engine, call, paths, threads);
                if (threads == 1) {
                    baseline_runtime = measurement.median_runtime;
                }
                const double throughput = static_cast<double>(paths) / measurement.median_runtime;
                const double speedup = baseline_runtime / measurement.median_runtime;
                const double efficiency = speedup / static_cast<double>(threads);

                output << paths << ',' << threads << ',' << kRepetitions;
                for (const double runtime : measurement.runtimes) {
                    output << ',' << runtime;
                }
                output << ',' << measurement.median_runtime << ',' << throughput << ','
                       << speedup << ',' << efficiency << ',' << measurement.price << ','
                       << measurement.standard_error << '\n';

                std::cout << std::setw(10) << paths << " paths / " << threads << " threads: "
                          << measurement.median_runtime << " s, " << speedup << "x speedup\n";
            }
        }
        if (!output) {
            throw std::runtime_error{"failed while writing scaling output"};
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Scaling benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
