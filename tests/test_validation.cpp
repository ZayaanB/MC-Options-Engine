#include <limits>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/simulation_config.hpp"
#include "mc/validation.hpp"

TEST_CASE("market validation enforces finite financial inputs", "[validation][market]") {
    constexpr mc::MarketData valid{
        .spot = 100.0,
        .risk_free_rate = -0.01,
        .volatility = 0.0,
    };
    REQUIRE_NOTHROW(mc::validate(valid));

    auto market = valid;
    market.spot = 0.0;
    REQUIRE_THROWS_AS(mc::validate(market), std::invalid_argument);

    market = valid;
    market.spot = -1.0;
    REQUIRE_THROWS_AS(mc::validate(market), std::invalid_argument);

    market = valid;
    market.volatility = -0.01;
    REQUIRE_THROWS_AS(mc::validate(market), std::invalid_argument);

    market = valid;
    market.risk_free_rate = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(mc::validate(market), std::invalid_argument);

    market = valid;
    market.volatility = std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(mc::validate(market), std::invalid_argument);
}

TEST_CASE("option validation accepts immediate expiry and rejects invalid inputs",
          "[validation][option]") {
    constexpr mc::OptionParameters valid{
        .strike = 100.0,
        .maturity = 0.0,
    };
    REQUIRE_NOTHROW(mc::validate(valid));

    auto option = valid;
    option.strike = 0.0;
    REQUIRE_THROWS_AS(mc::validate(option), std::invalid_argument);

    option = valid;
    option.strike = -1.0;
    REQUIRE_THROWS_AS(mc::validate(option), std::invalid_argument);

    option = valid;
    option.maturity = -1.0;
    REQUIRE_THROWS_AS(mc::validate(option), std::invalid_argument);

    option = valid;
    option.maturity = std::numeric_limits<double>::quiet_NaN();
    REQUIRE_THROWS_AS(mc::validate(option), std::invalid_argument);
}

TEST_CASE("simulation validation enforces execution invariants", "[validation][simulation]") {
    constexpr mc::SimulationConfig valid{
        .num_paths = 2,
        .seed = 42,
        .num_threads = 1,
        .batch_size = 0,
        .antithetic = false,
    };
    REQUIRE_NOTHROW(mc::validate(valid));

    auto config = valid;
    config.num_paths = 0;
    REQUIRE_THROWS_AS(mc::validate(config), std::invalid_argument);

    config = valid;
    config.num_threads = 0;
    REQUIRE_THROWS_AS(mc::validate(config), std::invalid_argument);

    config = valid;
    config.num_paths = 3;
    config.antithetic = true;
    REQUIRE_THROWS_WITH(mc::validate(config),
                        "antithetic simulation requires an even number of paths");

    config.num_paths = 4;
    REQUIRE_NOTHROW(mc::validate(config));
}
