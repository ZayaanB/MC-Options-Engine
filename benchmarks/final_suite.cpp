#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <thread>

#include "mc/greeks_result.hpp"
#include "mc/instruments/arithmetic_asian_call.hpp"
#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/greeks_engine.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/pricing/path_monte_carlo_engine.hpp"
#include "mc/pricing_result.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr std::uint64_t kSeed = 42;
constexpr std::size_t kRepetitions = 3;
constexpr std::array<std::uint64_t, 5> kConvergencePaths{
    1'000, 10'000, 100'000, 1'000'000, 5'000'000};
constexpr std::array<std::size_t, 4> kCandidateThreads{1, 2, 4, 8};
constexpr std::array<std::uint64_t, 3> kGreekPaths{100'000, 500'000, 1'000'000};
constexpr std::uint64_t kScalingPaths = 5'000'000;
constexpr std::uint64_t kVariancePaths = 5'000'000;
constexpr std::uint64_t kInstrumentPaths = 250'000;
constexpr std::size_t kAsianSteps = 252;

struct PricingMeasurement {
    mc::PricingResult result;
    std::array<double, kRepetitions> runtimes{};
    double median_runtime{};
};

struct GreeksMeasurement {
    mc::GreeksResult result;
    std::array<double, kRepetitions> runtimes{};
    double median_runtime{};
};

std::ofstream open_csv(const std::filesystem::path& path) {
    std::ofstream output{path};
    if (!output) {
        throw std::runtime_error{"could not open benchmark output: " + path.string()};
    }
    output << std::setprecision(17);
    return output;
}

template <typename Callable>
PricingMeasurement measure_price(Callable&& price) {
    PricingMeasurement measurement;
    for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
        const mc::PricingResult result = price();
        if (result.paths == 0 || result.observations == 0 ||
            !std::isfinite(result.price) || !std::isfinite(result.standard_error) ||
            !(result.runtime_seconds > 0.0)) {
            throw std::runtime_error{"pricing benchmark produced an invalid result"};
        }
        if (repetition == 0) {
            measurement.result = result;
        } else if (result.price != measurement.result.price ||
                   result.sample_variance != measurement.result.sample_variance ||
                   result.standard_error != measurement.result.standard_error) {
            throw std::runtime_error{
                "identical pricing benchmark configuration was not reproducible"};
        }
        measurement.runtimes[repetition] = result.runtime_seconds;
    }
    auto sorted = measurement.runtimes;
    std::sort(sorted.begin(), sorted.end());
    measurement.median_runtime = sorted[kRepetitions / 2];
    return measurement;
}

template <typename Callable>
GreeksMeasurement measure_greeks(Callable&& calculate) {
    GreeksMeasurement measurement;
    for (std::size_t repetition = 0; repetition < kRepetitions; ++repetition) {
        const auto start = std::chrono::steady_clock::now();
        const mc::GreeksResult result = calculate();
        const auto stop = std::chrono::steady_clock::now();
        if (!std::isfinite(result.delta) || !std::isfinite(result.gamma) ||
            !std::isfinite(result.vega)) {
            throw std::runtime_error{"Greeks benchmark produced an invalid result"};
        }
        if (repetition == 0) {
            measurement.result = result;
        } else if (result.delta != measurement.result.delta ||
                   result.gamma != measurement.result.gamma ||
                   result.vega != measurement.result.vega) {
            throw std::runtime_error{
                "identical Greeks benchmark configuration was not reproducible"};
        }
        measurement.runtimes[repetition] =
            std::chrono::duration<double>(stop - start).count();
    }
    auto sorted = measurement.runtimes;
    std::sort(sorted.begin(), sorted.end());
    measurement.median_runtime = sorted[kRepetitions / 2];
    return measurement;
}

void write_runtimes(std::ofstream& output, const std::array<double, kRepetitions>& runtimes,
                    const double median_runtime) {
    for (const double runtime : runtimes) {
        output << ',' << runtime;
    }
    output << ',' << median_runtime;
}

std::size_t available_thread_limit() {
    const auto hardware_threads = std::thread::hardware_concurrency();
    return hardware_threads == 0 ? 1 : static_cast<std::size_t>(hardware_threads);
}

std::size_t preferred_thread_count(const std::size_t limit) {
    return std::min<std::size_t>(4, limit);
}

void run_convergence(const std::filesystem::path& output_dir,
                     const mc::MonteCarloEngine& engine, const mc::EuropeanCall& call) {
    auto output = open_csv(output_dir / "convergence.csv");
    output << "paths,seed,threads,mc_price,analytical_price,absolute_error,standard_error,"
              "confidence_lower,confidence_upper,runtime_1_seconds,runtime_2_seconds,"
              "runtime_3_seconds,median_runtime_seconds\n";
    const double analytical = mc::black_scholes_call(kMarket, kOption);
    for (const auto paths : kConvergencePaths) {
        const mc::SimulationConfig config{paths, kSeed, 1, 0, false};
        const auto measurement = measure_price([&] { return engine.price(call, kMarket, kOption, config); });
        const auto& result = measurement.result;
        output << paths << ',' << kSeed << ",1," << result.price << ',' << analytical << ','
               << std::abs(result.price - analytical) << ',' << result.standard_error << ','
               << result.confidence_lower << ',' << result.confidence_upper;
        write_runtimes(output, measurement.runtimes, measurement.median_runtime);
        output << '\n';
    }
    std::cout << "A/5 convergence -> " << output_dir / "convergence.csv" << '\n';
}

void run_scaling(const std::filesystem::path& output_dir, const mc::MonteCarloEngine& engine,
                 const mc::EuropeanCall& call, const std::size_t thread_limit) {
    auto output = open_csv(output_dir / "scaling.csv");
    output << "paths,seed,threads,repetitions,runtime_1_seconds,runtime_2_seconds,"
              "runtime_3_seconds,median_runtime_seconds,throughput_paths_per_second,speedup,"
              "parallel_efficiency,mc_price,standard_error\n";
    double baseline_runtime = 0.0;
    for (const auto threads : kCandidateThreads) {
        if (threads > thread_limit) {
            continue;
        }
        const mc::SimulationConfig config{kScalingPaths, kSeed, threads, 0, false};
        const auto measurement = measure_price([&] { return engine.price(call, kMarket, kOption, config); });
        if (threads == 1) {
            baseline_runtime = measurement.median_runtime;
        }
        const double throughput = static_cast<double>(kScalingPaths) / measurement.median_runtime;
        const double speedup = baseline_runtime / measurement.median_runtime;
        output << kScalingPaths << ',' << kSeed << ',' << threads << ',' << kRepetitions;
        write_runtimes(output, measurement.runtimes, measurement.median_runtime);
        output << ',' << throughput << ',' << speedup << ','
               << speedup / static_cast<double>(threads) << ',' << measurement.result.price << ','
               << measurement.result.standard_error << '\n';
    }
    std::cout << "B/5 scaling -> " << output_dir / "scaling.csv" << '\n';
}

void run_variance_reduction(const std::filesystem::path& output_dir,
                            const mc::MonteCarloEngine& engine,
                            const mc::EuropeanCall& call, const std::size_t threads) {
    auto output = open_csv(output_dir / "variance_reduction.csv");
    output << "paths,seed,threads,mode,observations,mc_price,sample_variance,"
              "estimator_variance,standard_error,runtime_1_seconds,runtime_2_seconds,"
              "runtime_3_seconds,median_runtime_seconds,variance_reduction_vs_standard\n";
    double standard_estimator_variance = 0.0;
    for (const bool antithetic : {false, true}) {
        const mc::SimulationConfig config{kVariancePaths, kSeed, threads, 0, antithetic};
        const auto measurement = measure_price([&] { return engine.price(call, kMarket, kOption, config); });
        const auto& result = measurement.result;
        const double estimator_variance = result.standard_error * result.standard_error;
        if (!antithetic) {
            standard_estimator_variance = estimator_variance;
        }
        output << kVariancePaths << ',' << kSeed << ',' << threads << ','
               << (antithetic ? "antithetic" : "standard") << ',' << result.observations << ','
               << result.price << ',' << result.sample_variance << ',' << estimator_variance << ','
               << result.standard_error;
        write_runtimes(output, measurement.runtimes, measurement.median_runtime);
        output << ',' << standard_estimator_variance / estimator_variance << '\n';
    }
    std::cout << "C/5 variance reduction -> " << output_dir / "variance_reduction.csv" << '\n';
}

void write_instrument_row(std::ofstream& output, const std::string_view instrument,
                          const std::size_t steps, const std::size_t threads,
                          const PricingMeasurement& measurement) {
    const auto& result = measurement.result;
    const double paths_per_second = static_cast<double>(kInstrumentPaths) /
                                    measurement.median_runtime;
    const double steps_per_second = paths_per_second * static_cast<double>(steps);
    output << instrument << ',' << kInstrumentPaths << ',' << kSeed << ',' << threads << ','
           << steps << ',' << result.price << ',' << result.standard_error;
    write_runtimes(output, measurement.runtimes, measurement.median_runtime);
    output << ',' << paths_per_second << ',' << steps_per_second << '\n';
}

void run_instrument_performance(const std::filesystem::path& output_dir,
                                const mc::MonteCarloEngine& terminal_engine,
                                const mc::PathMonteCarloEngine& path_engine,
                                const mc::EuropeanCall& call,
                                const mc::ArithmeticAsianCall& asian_call,
                                const std::size_t thread_limit) {
    auto output = open_csv(output_dir / "instrument_performance.csv");
    output << "instrument,paths,seed,threads,steps,mc_price,standard_error,runtime_1_seconds,"
              "runtime_2_seconds,runtime_3_seconds,median_runtime_seconds,"
              "throughput_paths_per_second,throughput_steps_per_second\n";
    for (const auto threads : kCandidateThreads) {
        if (threads > thread_limit) {
            continue;
        }
        const mc::SimulationConfig european_config{kInstrumentPaths, kSeed, threads, 0, false, 1};
        const auto european = measure_price(
            [&] { return terminal_engine.price(call, kMarket, kOption, european_config); });
        write_instrument_row(output, "european_call", 1, threads, european);

        const mc::SimulationConfig asian_config{
            kInstrumentPaths, kSeed, threads, 0, false, kAsianSteps};
        const auto asian = measure_price(
            [&] { return path_engine.price(asian_call, kMarket, kOption, asian_config); });
        write_instrument_row(output, "asian_call", kAsianSteps, threads, asian);
    }
    std::cout << "D/5 instrument performance -> "
              << output_dir / "instrument_performance.csv" << '\n';
}

mc::GreeksResult analytical_greeks() {
    const double root_maturity = std::sqrt(kOption.maturity);
    const double d1 = (std::log(kMarket.spot / kOption.strike) +
                       (kMarket.risk_free_rate + 0.5 * kMarket.volatility * kMarket.volatility) *
                           kOption.maturity) /
                      (kMarket.volatility * root_maturity);
    const double density = std::exp(-0.5 * d1 * d1) /
                           std::sqrt(2.0 * std::numbers::pi);
    return {
        .delta = mc::standard_normal_cdf(d1),
        .gamma = density / (kMarket.spot * kMarket.volatility * root_maturity),
        .vega = kMarket.spot * density * root_maturity * 0.01,
    };
}

void write_greek_row(std::ofstream& output, const std::uint64_t paths,
                     const std::size_t threads, const std::string_view greek,
                     const double estimate, const double analytical,
                     const GreeksMeasurement& measurement) {
    const double absolute_error = std::abs(estimate - analytical);
    output << paths << ',' << kSeed << ',' << threads << ",antithetic," << greek << ','
           << estimate << ',' << analytical << ',' << absolute_error << ','
           << absolute_error / std::abs(analytical);
    write_runtimes(output, measurement.runtimes, measurement.median_runtime);
    output << '\n';
}

void run_greeks_accuracy(const std::filesystem::path& output_dir,
                         const mc::GreeksEngine& engine, const mc::EuropeanCall& call,
                         const std::size_t threads) {
    auto output = open_csv(output_dir / "greeks_accuracy.csv");
    output << "paths,seed,threads,mode,greek,mc_estimate,analytical_value,absolute_error,"
              "relative_error,runtime_1_seconds,runtime_2_seconds,runtime_3_seconds,"
              "median_runtime_seconds\n";
    const mc::GreeksResult analytical = analytical_greeks();
    for (const auto paths : kGreekPaths) {
        const mc::SimulationConfig config{paths, kSeed, threads, 0, true};
        const auto measurement = measure_greeks(
            [&] { return engine.calculate(call, kMarket, kOption, config); });
        write_greek_row(output, paths, threads, "delta", measurement.result.delta,
                        analytical.delta, measurement);
        write_greek_row(output, paths, threads, "gamma", measurement.result.gamma,
                        analytical.gamma, measurement);
        write_greek_row(output, paths, threads, "vega_per_volatility_point",
                        measurement.result.vega, analytical.vega, measurement);
    }
    std::cout << "E/5 Greeks accuracy -> " << output_dir / "greeks_accuracy.csv" << '\n';
}

}

int main(const int argc, const char* const argv[]) {
    try {
        if (argc > 2) {
            throw std::invalid_argument{"usage: mc_final_benchmarks [output-directory]"};
        }
        const std::filesystem::path output_dir =
            argc == 2 ? std::filesystem::path{argv[1]}
                      : std::filesystem::path{"results/final"};
        std::filesystem::create_directories(output_dir);

        const std::size_t thread_limit = available_thread_limit();
        const std::size_t preferred_threads = preferred_thread_count(thread_limit);
        const mc::EuropeanCall call{kOption.strike};
        const mc::ArithmeticAsianCall asian_call{kOption.strike};
        const mc::MonteCarloEngine terminal_engine;
        const mc::PathMonteCarloEngine path_engine;
        const mc::GreeksEngine greeks_engine;

        (void)terminal_engine.price(call, kMarket, kOption, {100'000, kSeed, 1, 0, false});
        run_convergence(output_dir, terminal_engine, call);
        run_scaling(output_dir, terminal_engine, call, thread_limit);
        run_variance_reduction(output_dir, terminal_engine, call, preferred_threads);
        run_instrument_performance(output_dir, terminal_engine, path_engine, call, asian_call,
                                   thread_limit);
        run_greeks_accuracy(output_dir, greeks_engine, call, preferred_threads);
        std::cout << "Final benchmark suite complete (seed " << kSeed << ", up to "
                  << std::min<std::size_t>(8, thread_limit) << " threads).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Final benchmark suite failed: " << error.what() << '\n';
        return 1;
    }
}
