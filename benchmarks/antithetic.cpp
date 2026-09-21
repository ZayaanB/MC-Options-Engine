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
#include "mc/pricing_result.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::array<std::uint64_t, 2> kPathCounts{1'000'000, 5'000'000};
constexpr std::array<std::size_t, 3> kThreadCounts{1, 2, 4};
constexpr std::size_t kRepetitions = 3;

struct Measurement {
    mc::PricingResult result;
    std::array<double, kRepetitions> runtimes{};
    double median_runtime{};
};

Measurement measure(const mc::MonteCarloEngine& engine, const mc::EuropeanCall& call,
                    const std::uint64_t paths, const std::size_t threads,
                    const bool antithetic) {
    const mc::SimulationConfig config{paths, 42, threads, 0, antithetic};
    Measurement measurement;
    for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
        const auto result = engine.price(call, kMarket, kOption, config);
        if (result.paths != paths ||
            result.observations != paths / (antithetic ? 2 : 1) ||
            !std::isfinite(result.standard_error) || result.runtime_seconds <= 0.0) {
            throw std::runtime_error{"invalid antithetic benchmark result"};
        }
        if (repetition > 0 &&
            (result.price != measurement.result.price ||
             result.sample_variance != measurement.result.sample_variance)) {
            throw std::runtime_error{"identical benchmark configuration was not reproducible"};
        }
        if (repetition == 0) {
            measurement.result = result;
        }
        measurement.runtimes[repetition] = result.runtime_seconds;
    }
    auto sorted = measurement.runtimes;
    std::sort(sorted.begin(), sorted.end());
    measurement.median_runtime = sorted[kRepetitions / 2];
    return measurement;
}

void write_row(std::ofstream& output, const std::uint64_t paths, const std::size_t threads,
               const bool antithetic, const Measurement& measurement) {
    const auto& result = measurement.result;
    output << paths << ',' << threads << ',' << (antithetic ? "antithetic" : "standard")
           << ',' << result.observations << ',' << result.price << ','
           << result.sample_variance << ',' << result.standard_error * result.standard_error
           << ',' << result.standard_error;
    for (const double runtime : measurement.runtimes) {
        output << ',' << runtime;
    }
    output << ',' << measurement.median_runtime << '\n';
}

}  // namespace

int main(const int argc, const char* const argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument{"usage: mc_antithetic [output.csv]"};
        }
        const std::filesystem::path output_path =
            argc == 2 ? std::filesystem::path{argv[1]}
                      : std::filesystem::path{"results/antithetic.csv"};
        const auto available_threads = std::thread::hardware_concurrency();
        const std::size_t max_threads = available_threads == 0 ? 1 : available_threads;
        const mc::EuropeanCall call{kOption.strike};
        const mc::MonteCarloEngine engine;

        (void)engine.price(call, kMarket, kOption, {100'000, 42, 1, 0, false});
        (void)engine.price(call, kMarket, kOption, {100'000, 42, 1, 0, true});

        if (!output_path.parent_path().empty()) {
            std::filesystem::create_directories(output_path.parent_path());
        }
        std::ofstream output{output_path};
        if (!output) {
            throw std::runtime_error{"could not open benchmark output: " + output_path.string()};
        }
        output << "paths,threads,mode,observations,mc_price,sample_variance,"
                  "estimator_variance,standard_error,runtime_1_seconds,runtime_2_seconds,"
                  "runtime_3_seconds,median_runtime_seconds\n";
        output << std::setprecision(17);

        std::cout << "Antithetic benchmark (equal trajectory counts, fixed seed)\n"
                  << std::fixed << std::setprecision(5);
        for (const auto paths : kPathCounts) {
            for (const auto threads : kThreadCounts) {
                if (threads > max_threads) {
                    continue;
                }
                const auto standard = measure(engine, call, paths, threads, false);
                const auto antithetic = measure(engine, call, paths, threads, true);
                write_row(output, paths, threads, false, standard);
                write_row(output, paths, threads, true, antithetic);

                const double variance_ratio =
                    standard.result.standard_error * standard.result.standard_error /
                    (antithetic.result.standard_error * antithetic.result.standard_error);
                std::cout << paths << " paths, " << threads << " threads: estimator variance "
                          << variance_ratio << "x lower; median runtimes "
                          << standard.median_runtime << " s standard / "
                          << antithetic.median_runtime << " s antithetic\n";
            }
        }
        if (!output) {
            throw std::runtime_error{"failed while writing antithetic benchmark output"};
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Antithetic benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
