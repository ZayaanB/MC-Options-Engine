#include <array>
#include <cstdint>
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mc/cli/price_options.hpp"

TEST_CASE("price CLI uses the canonical European call defaults", "[cli]") {
    constexpr std::array<std::string_view, 0> arguments{};
    const auto options = mc::cli::parse_price_options(arguments);

    REQUIRE(options.type == mc::cli::OptionType::call);
    REQUIRE(options.method == mc::cli::PricingMethod::both);
    REQUIRE(options.market.spot == 100.0);
    REQUIRE(options.market.risk_free_rate == 0.05);
    REQUIRE(options.market.volatility == 0.20);
    REQUIRE(options.option.strike == 100.0);
    REQUIRE(options.option.maturity == 1.0);
    REQUIRE(options.simulation.num_paths == 1'000'000);
    REQUIRE(options.simulation.num_threads == 1);
    REQUIRE(options.simulation.seed == 42);
    REQUIRE_FALSE(options.simulation.antithetic);
}

TEST_CASE("price CLI parses market and simulation configuration", "[cli]") {
    constexpr std::array arguments{
        std::string_view{"--type"},       std::string_view{"put"},
        std::string_view{"--method"},     std::string_view{"mc"},
        std::string_view{"--spot"},       std::string_view{"98.5"},
        std::string_view{"--strike"},     std::string_view{"105"},
        std::string_view{"--rate"},       std::string_view{"-0.01"},
        std::string_view{"--volatility"}, std::string_view{"0.25"},
        std::string_view{"--maturity"},   std::string_view{"0.5"},
        std::string_view{"--paths"},      std::string_view{"5000000"},
        std::string_view{"--threads"},    std::string_view{"8"},
        std::string_view{"--seed"},       std::string_view{"123"},
        std::string_view{"--antithetic"},
    };
    const auto options = mc::cli::parse_price_options(arguments);

    REQUIRE(options.type == mc::cli::OptionType::put);
    REQUIRE(options.method == mc::cli::PricingMethod::monte_carlo);
    REQUIRE(options.market.spot == 98.5);
    REQUIRE(options.market.risk_free_rate == -0.01);
    REQUIRE(options.market.volatility == 0.25);
    REQUIRE(options.option.strike == 105.0);
    REQUIRE(options.option.maturity == 0.5);
    REQUIRE(options.simulation.num_paths == 5'000'000);
    REQUIRE(options.simulation.num_threads == 8);
    REQUIRE(options.simulation.seed == 123);
    REQUIRE(options.simulation.antithetic);
}

TEST_CASE("price CLI accepts all pricing methods", "[cli]") {
    constexpr std::array analytical{std::string_view{"--method"},
                                    std::string_view{"analytical"}};
    constexpr std::array both{std::string_view{"--method"}, std::string_view{"both"}};

    REQUIRE(mc::cli::parse_price_options(analytical).method ==
            mc::cli::PricingMethod::analytical);
    REQUIRE(mc::cli::parse_price_options(both).method == mc::cli::PricingMethod::both);
}

TEST_CASE("price CLI rejects malformed arguments", "[cli][validation]") {
    constexpr std::array missing_value{std::string_view{"--spot"}};
    constexpr std::array bad_number{std::string_view{"--spot"}, std::string_view{"100usd"}};
    constexpr std::array nonfinite{std::string_view{"--rate"}, std::string_view{"nan"}};
    constexpr std::array negative_integer{std::string_view{"--paths"},
                                          std::string_view{"-1"}};
    constexpr std::array bad_type{std::string_view{"--type"}, std::string_view{"straddle"}};
    constexpr std::array bad_method{std::string_view{"--method"},
                                    std::string_view{"closed-form"}};
    constexpr std::array unknown{std::string_view{"--dividend"}, std::string_view{"0.01"}};
    constexpr std::array positional{std::string_view{"call"}};
    constexpr std::array duplicate{std::string_view{"--seed"}, std::string_view{"1"},
                                   std::string_view{"--seed"}, std::string_view{"2"}};

    REQUIRE_THROWS_AS(mc::cli::parse_price_options(missing_value), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(bad_number), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(nonfinite), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(negative_integer), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(bad_type), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(bad_method), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(unknown), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(positional), std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_price_options(duplicate), std::invalid_argument);
}
