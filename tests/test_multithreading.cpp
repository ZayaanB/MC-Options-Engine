#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <mutex>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#include "mc/instruments/european_call.hpp"
#include "mc/instruments/instrument.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};

mc::SimulationConfig configuration(const std::uint64_t paths, const std::size_t threads) {
    return {
        .num_paths = paths,
        .seed = 42,
        .num_threads = threads,
        .batch_size = 0,
        .antithetic = false,
    };
}

class CountingCall final : public mc::Instrument {
public:
    double payoff(const double terminal_price) const noexcept override {
        count_.fetch_add(1, std::memory_order_relaxed);
        return terminal_price > 100.0 ? terminal_price - 100.0 : 0.0;
    }

    std::uint64_t count() const noexcept { return count_.load(std::memory_order_relaxed); }

private:
    mutable std::atomic<std::uint64_t> count_{0};
};

class WorkerCountingCall final : public mc::Instrument {
public:
    double payoff(const double terminal_price) const noexcept override {
        const std::lock_guard lock{mutex_};
        const auto id = std::this_thread::get_id();
        for (std::size_t slot = 0; slot < worker_ids_.size(); ++slot) {
            if (worker_ids_[slot] == id || worker_ids_[slot] == std::thread::id{}) {
                worker_ids_[slot] = id;
                ++counts_[slot];
                break;
            }
        }
        return terminal_price > 100.0 ? terminal_price - 100.0 : 0.0;
    }

    std::array<std::uint64_t, 8> counts() const noexcept {
        const std::lock_guard lock{mutex_};
        return counts_;
    }

private:
    mutable std::mutex mutex_;
    mutable std::array<std::thread::id, 8> worker_ids_{};
    mutable std::array<std::uint64_t, 8> counts_{};
};

}  // namespace

TEST_CASE("four workers price a call within statistical uncertainty", "[monte-carlo][threads]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = configuration(250'000, 4);

    const auto result = engine.price(call, kMarket, kOption, config);
    const double analytical = mc::black_scholes_call(kMarket, kOption);

    REQUIRE(result.paths == config.num_paths);
    REQUIRE(result.standard_error > 0.0);
    REQUIRE(std::abs(result.price - analytical) < 3.0 * result.standard_error);
}

TEST_CASE("uneven path division executes each path exactly once", "[monte-carlo][threads]") {
    const CountingCall call;
    const mc::MonteCarloEngine engine;
    const auto config = configuration(10'003, 4);

    const auto result = engine.price(call, kMarket, kOption, config);

    REQUIRE(call.count() == config.num_paths);
    REQUIRE(result.paths == config.num_paths);
    REQUIRE(std::isfinite(result.price));
}

TEST_CASE("more requested threads than paths does not lose work", "[monte-carlo][threads]") {
    const CountingCall call;
    const mc::MonteCarloEngine engine;
    const auto config = configuration(2, 8);

    const auto result = engine.price(call, kMarket, kOption, config);

    REQUIRE(call.count() == 2);
    REQUIRE(result.paths == 2);
    REQUIRE(std::isfinite(result.standard_error));
}

TEST_CASE("same multithreaded configuration reproduces the estimate",
          "[monte-carlo][threads][rng]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = configuration(10'003, 4);

    const auto first = engine.price(call, kMarket, kOption, config);
    const auto second = engine.price(call, kMarket, kOption, config);

    REQUIRE(first.price == second.price);
    REQUIRE(first.sample_variance == second.sample_variance);
    REQUIRE(first.standard_error == second.standard_error);
}

TEST_CASE("workers receive balanced disjoint path counts", "[monte-carlo][threads]") {
    const WorkerCountingCall call;
    const mc::MonteCarloEngine engine;

    const auto result = engine.price(call, kMarket, kOption, configuration(17, 4));
    auto counts = call.counts();
    std::sort(counts.begin(), counts.end());

    REQUIRE(result.paths == 17);
    REQUIRE(counts == std::array<std::uint64_t, 8>{0, 0, 0, 0, 4, 4, 4, 5});
}

TEST_CASE("thread counts from one to eight remain statistically consistent",
          "[monte-carlo][threads]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    const double analytical = mc::black_scholes_call(kMarket, kOption);

    for (const std::size_t threads : {1U, 2U, 4U, 8U}) {
        const auto result = engine.price(call, kMarket, kOption, configuration(250'000, threads));

        INFO("threads = " << threads);
        REQUIRE(result.paths == 250'000);
        REQUIRE(std::abs(result.price - analytical) < 4.0 * result.standard_error);
        REQUIRE(result.confidence_lower <= result.price);
        REQUIRE(result.price <= result.confidence_upper);
    }
}

TEST_CASE("parallel reduction is bit reproducible for the same full configuration",
          "[monte-carlo][threads][rng]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = configuration(31'337, 8);
    const auto reference = engine.price(call, kMarket, kOption, config);

    for (int repetition = 0; repetition < 10; ++repetition) {
        const auto result = engine.price(call, kMarket, kOption, config);
        REQUIRE(result.paths == reference.paths);
        REQUIRE(result.price == reference.price);
        REQUIRE(result.sample_variance == reference.sample_variance);
        REQUIRE(result.standard_error == reference.standard_error);
        REQUIRE(result.confidence_lower == reference.confidence_lower);
        REQUIRE(result.confidence_upper == reference.confidence_upper);
    }
}

TEST_CASE("antithetic work remains paired and reproducible across workers",
          "[monte-carlo][threads][antithetic]") {
    const CountingCall call;
    const mc::MonteCarloEngine engine;
    auto config = configuration(14, 4);
    config.antithetic = true;

    const auto first = engine.price(call, kMarket, kOption, config);
    const auto second = engine.price(call, kMarket, kOption, config);

    REQUIRE(call.count() == 28);
    REQUIRE(first.paths == 14);
    REQUIRE(first.observations == 7);
    REQUIRE(first.price == second.price);
    REQUIRE(first.sample_variance == second.sample_variance);
    REQUIRE(first.standard_error == second.standard_error);
}

TEST_CASE("more workers than antithetic pairs does not create empty work",
          "[monte-carlo][threads][antithetic]") {
    const CountingCall call;
    const mc::MonteCarloEngine engine;
    auto config = configuration(4, 8);
    config.antithetic = true;

    const auto result = engine.price(call, kMarket, kOption, config);

    REQUIRE(call.count() == 4);
    REQUIRE(result.paths == 4);
    REQUIRE(result.observations == 2);
    REQUIRE(std::isfinite(result.standard_error));
}
