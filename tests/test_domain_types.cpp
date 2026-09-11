#include <cstddef>
#include <cstdint>

#include <catch2/catch_test_macros.hpp>

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/simulation_config.hpp"

TEST_CASE("market and option inputs are represented as value types", "[domain]") {
    constexpr mc::MarketData market{
        .spot = 100.0,
        .risk_free_rate = 0.05,
        .volatility = 0.20,
    };
    constexpr mc::OptionParameters option{
        .strike = 105.0,
        .maturity = 1.5,
    };

    STATIC_REQUIRE(market.spot == 100.0);
    STATIC_REQUIRE(market.risk_free_rate == 0.05);
    STATIC_REQUIRE(market.volatility == 0.20);
    STATIC_REQUIRE(option.strike == 105.0);
    STATIC_REQUIRE(option.maturity == 1.5);
}

TEST_CASE("simulation inputs retain the complete execution configuration", "[domain]") {
    constexpr mc::SimulationConfig config{
        .num_paths = std::uint64_t{1'000'000},
        .seed = std::uint64_t{42},
        .num_threads = std::size_t{8},
        .batch_size = std::size_t{16'384},
        .antithetic = true,
    };

    STATIC_REQUIRE(config.num_paths == 1'000'000);
    STATIC_REQUIRE(config.seed == 42);
    STATIC_REQUIRE(config.num_threads == 8);
    STATIC_REQUIRE(config.batch_size == 16'384);
    STATIC_REQUIRE(config.antithetic);
}
