#include <cmath>
#include <cstdint>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/instruments/arithmetic_asian_call.hpp"
#include "mc/instruments/european_call.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/pricing/path_monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};

mc::SimulationConfig config(const std::uint64_t paths = 100'000,
                            const std::size_t steps = 12) {
    return {paths, 42, 1, 0, false, steps};
}

}

TEST_CASE("arithmetic Asian call pays on the monitored arithmetic average", "[asian][payoff]") {
    const mc::ArithmeticAsianCall call{100.0};

    REQUIRE(call.strike() == 100.0);
    REQUIRE(call.payoff(90.0) == 0.0);
    REQUIRE(call.payoff(100.0) == 0.0);
    REQUIRE(call.payoff(112.5) == 12.5);
}

TEST_CASE("one monitoring step matches terminal European call Monte Carlo",
          "[asian][monte-carlo]") {
    const mc::ArithmeticAsianCall asian{kOption.strike};
    const mc::EuropeanCall european{kOption.strike};
    const mc::PathMonteCarloEngine path_engine;
    const mc::MonteCarloEngine terminal_engine;
    const auto simulation = config(100'000, 1);

    const auto path_result = path_engine.price(asian, kMarket, kOption, simulation);
    const auto terminal_result = terminal_engine.price(european, kMarket, kOption, simulation);

    REQUIRE(path_result.price == terminal_result.price);
    REQUIRE(path_result.sample_variance == terminal_result.sample_variance);
    REQUIRE(path_result.standard_error == terminal_result.standard_error);
    REQUIRE(path_result.confidence_lower == terminal_result.confidence_lower);
    REQUIRE(path_result.confidence_upper == terminal_result.confidence_upper);
}

TEST_CASE("Asian monitoring excludes initial spot and includes maturity",
          "[asian][boundary]") {
    constexpr mc::MarketData deterministic_market{100.0, 0.10, 0.0};
    constexpr mc::OptionParameters option{90.0, 1.0};
    const mc::ArithmeticAsianCall call{option.strike};
    const mc::PathMonteCarloEngine engine;
    const auto simulation = config(100, 2);
    const auto result = engine.price(call, deterministic_market, option, simulation);

    const double first_monitor = deterministic_market.spot * std::exp(0.05);
    const double maturity_monitor = deterministic_market.spot * std::exp(0.10);
    const double average = 0.5 * (first_monitor + maturity_monitor);
    const double expected = std::exp(-0.10) * std::max(average - option.strike, 0.0);

    REQUIRE(result.price == Catch::Approx(expected).epsilon(1e-12));
    REQUIRE(result.sample_variance == Catch::Approx(0.0).margin(1e-24));
    REQUIRE(result.standard_error == Catch::Approx(0.0).margin(1e-24));
}

TEST_CASE("Asian call supports zero maturity", "[asian][boundary]") {
    constexpr mc::OptionParameters expired{90.0, 0.0};
    const mc::ArithmeticAsianCall call{expired.strike};
    const mc::PathMonteCarloEngine engine;
    const auto result = engine.price(call, kMarket, expired, config(100, 252));

    REQUIRE(result.price == Catch::Approx(10.0));
    REQUIRE(result.sample_variance == Catch::Approx(0.0).margin(1e-24));
}

TEST_CASE("expired Asian call avoids overflowing an unnecessary path sum", "[asian][boundary]") {
    const mc::MarketData market{1e308, 0.0, 0.0};
    const mc::OptionParameters expired{1.0, 0.0};
    const mc::ArithmeticAsianCall call{1.0};
    auto simulation = config(4, 252);
    simulation.antithetic = true;
    const auto result = mc::PathMonteCarloEngine{}.price(call, market, expired, simulation);
    REQUIRE(result.price == 1e308);
    REQUIRE(result.standard_error == 0.0);
}

TEST_CASE("Asian call agrees with Black-Scholes when monitored only at maturity",
          "[asian][monte-carlo]") {
    const mc::ArithmeticAsianCall call{kOption.strike};
    const mc::PathMonteCarloEngine engine;
    const auto result = engine.price(call, kMarket, kOption, config(250'000, 1));

    REQUIRE(std::abs(result.price - mc::black_scholes_call(kMarket, kOption)) <
            3.0 * result.standard_error);
}

TEST_CASE("Asian antithetic mode counts trajectories and pair observations",
          "[asian][antithetic]") {
    const mc::ArithmeticAsianCall call{kOption.strike};
    const mc::PathMonteCarloEngine engine;
    auto simulation = config(100'000, 12);
    simulation.antithetic = true;
    simulation.num_threads = 4;

    const auto first = engine.price(call, kMarket, kOption, simulation);
    const auto second = engine.price(call, kMarket, kOption, simulation);

    REQUIRE(first.paths == simulation.num_paths);
    REQUIRE(first.observations == simulation.num_paths / 2);
    REQUIRE(first.standard_error > 0.0);
    REQUIRE(first.price == second.price);
    REQUIRE(first.sample_variance == second.sample_variance);
    REQUIRE(first.standard_error == second.standard_error);
}

TEST_CASE("Asian path pricing validates steps and antithetic path counts",
          "[asian][validation]") {
    const mc::ArithmeticAsianCall call{kOption.strike};
    const mc::PathMonteCarloEngine engine;

    auto simulation = config();
    simulation.num_steps = 0;
    REQUIRE_THROWS_AS(engine.price(call, kMarket, kOption, simulation),
                      std::invalid_argument);

    simulation = config(101);
    simulation.antithetic = true;
    REQUIRE_THROWS_AS(engine.price(call, kMarket, kOption, simulation),
                      std::invalid_argument);
}

TEST_CASE("batched Asian simulation preserves paths and reproducibility",
          "[asian][batch]") {
    const mc::ArithmeticAsianCall call{kOption.strike};
    const mc::PathMonteCarloEngine engine;
    auto unbatched_config = config(20'003, 12);
    unbatched_config.num_threads = 4;
    auto batched_config = unbatched_config;
    batched_config.batch_size = 31;

    const auto unbatched = engine.price(call, kMarket, kOption, unbatched_config);
    const auto batched = engine.price(call, kMarket, kOption, batched_config);
    const auto repeated = engine.price(call, kMarket, kOption, batched_config);

    REQUIRE(batched.paths == unbatched.paths);
    REQUIRE(batched.observations == unbatched.observations);
    REQUIRE(batched.price == Catch::Approx(unbatched.price).epsilon(1e-12));
    REQUIRE(batched.sample_variance ==
            Catch::Approx(unbatched.sample_variance).epsilon(1e-12));
    REQUIRE(batched.price == repeated.price);
    REQUIRE(batched.sample_variance == repeated.sample_variance);
}
