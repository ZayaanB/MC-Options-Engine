#include <algorithm>
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
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::uint64_t kPaths = 10'000'000;
constexpr std::size_t kRepetitions = 9;
constexpr std::array<std::size_t, 2> kThreadCounts{1, 8};

}

int main(const int argc, const char* const argv[]) {
    try {
        if (argc != 2) {
            throw std::invalid_argument{"usage: mc_hot_loop output.csv"};
        }
        const std::filesystem::path output_path{argv[1]};
        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open benchmark output: " + output_path.string()};
        }
        output << "paths,threads,repetitions,median_runtime_seconds,"
                  "throughput_paths_per_second,mc_price,standard_error\n"
               << std::setprecision(17);

        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine engine;
        (void)engine.price(call, kMarket, kOption, {1'000'000, 42, 1, 0, false, 1});

        std::cout << std::fixed << std::setprecision(6);
        for (const auto threads : kThreadCounts) {
            const mc::SimulationConfig config{kPaths, 42, threads, 0, false, 1};
            std::array<double, kRepetitions> runtimes{};
            double expected_price = 0.0;
            double expected_standard_error = 0.0;
            for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
                const auto result = engine.price(call, kMarket, kOption, config);
                if (repetition == 0) {
                    expected_price = result.price;
                    expected_standard_error = result.standard_error;
                } else if (result.price != expected_price ||
                           result.standard_error != expected_standard_error) {
                    throw std::runtime_error{"hot-loop benchmark was not reproducible"};
                }
                runtimes[repetition] = result.runtime_seconds;
            }
            std::sort(runtimes.begin(), runtimes.end());
            const double median = runtimes[kRepetitions / 2];
            const double throughput = static_cast<double>(kPaths) / median;
            output << kPaths << ',' << threads << ',' << kRepetitions << ',' << median << ','
                   << throughput << ',' << expected_price << ',' << expected_standard_error
                   << '\n';
            std::cout << threads << " thread(s): " << median << " s, "
                      << throughput / 1.0e6 << " M paths/s\n";
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Hot-loop benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
