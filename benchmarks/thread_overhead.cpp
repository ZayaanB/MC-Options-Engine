#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::array<std::uint64_t, 3> kPathCounts{8, 1'000, 1'000'000};
constexpr std::array<std::size_t, 2> kThreadCounts{1, 8};

std::size_t repetitions(const std::uint64_t paths) {
    return paths < 1'000'000 ? 101 : 11;
}

double median_runtime(const mc::MonteCarloEngine& engine, const mc::EuropeanCall& call,
                      const std::uint64_t paths, const std::size_t threads) {
    const mc::SimulationConfig config{paths, 42, threads, 0, false, 1};
    std::vector<double> runtimes;
    runtimes.reserve(repetitions(paths));
    double expected_price = 0.0;

    for (std::size_t repetition = 0; repetition < repetitions(paths); ++repetition) {
        const auto result = engine.price(call, kMarket, kOption, config);
        if (repetition == 0) {
            expected_price = result.price;
        } else if (result.price != expected_price) {
            throw std::runtime_error{"thread benchmark configuration was not reproducible"};
        }
        runtimes.push_back(result.runtime_seconds);
    }
    std::sort(runtimes.begin(), runtimes.end());
    return runtimes[runtimes.size() / 2];
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument{"usage: mc_thread_overhead [output.csv]"};
        }
        const std::filesystem::path output_path =
            argc == 2 ? std::filesystem::path{argv[1]}
                      : std::filesystem::path{"results/thread_overhead.csv"};
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open profile output: " + output_path.string()};
        }
        output << "paths,threads,repetitions,median_runtime_seconds,nanoseconds_per_path\n"
               << std::setprecision(17);

        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine engine;
        (void)engine.price(call, kMarket, kOption, {100'000, 42, 1, 0, false, 1});

        std::cout << "Thread launch and useful-work crossover\n" << std::fixed
                  << std::setprecision(6);
        for (const auto paths : kPathCounts) {
            for (const auto threads : kThreadCounts) {
                const double runtime = median_runtime(engine, call, paths, threads);
                const double nanoseconds_per_path =
                    runtime * 1.0e9 / static_cast<double>(paths);
                output << paths << ',' << threads << ',' << repetitions(paths) << ','
                       << runtime << ',' << nanoseconds_per_path << '\n';
                std::cout << paths << " paths / " << threads << " threads: "
                          << runtime * 1.0e6 << " us (" << nanoseconds_per_path
                          << " ns/path)\n";
            }
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Thread overhead benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
