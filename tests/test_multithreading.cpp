#include <atomic>
#include <cmath>
#include <cstdint>

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
