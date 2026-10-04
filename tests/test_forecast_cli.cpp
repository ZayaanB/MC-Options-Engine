#include <array>
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mc/cli/forecast_options.hpp"

TEST_CASE("forecast CLI parses its data and horizon configuration", "[forecast-cli]") {
    constexpr std::array arguments{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--price-column"}, std::string_view{"Close"},
        std::string_view{"--horizon-days"}, std::string_view{"63"},
        std::string_view{"--trading-days"}, std::string_view{"250"},
        std::string_view{"--volatility-model"}, std::string_view{"ewma"},
        std::string_view{"--ewma-decay"}, std::string_view{"0.90"},
    };
    const auto options = mc::cli::parse_forecast_options(arguments);

    REQUIRE(options.csv_path == "prices.csv");
    REQUIRE(options.price_column == "Close");
    REQUIRE(options.horizon_days == 63);
    REQUIRE(options.trading_days_per_year == 250.0);
    REQUIRE(options.volatility_estimator ==
            mc::forecasting::VolatilityEstimator::ewma);
    REQUIRE(options.ewma_decay == 0.90);
}

TEST_CASE("forecast CLI uses safe defaults", "[forecast-cli]") {
    constexpr std::array arguments{std::string_view{"--csv"},
                                   std::string_view{"prices.csv"}};
    const auto options = mc::cli::parse_forecast_options(arguments);

    REQUIRE(options.price_column == "Adj Close");
    REQUIRE(options.horizon_days == 20);
    REQUIRE(options.trading_days_per_year == 252.0);
    REQUIRE(options.volatility_estimator ==
            mc::forecasting::VolatilityEstimator::sample);
    REQUIRE(options.ewma_decay == 0.94);
}

TEST_CASE("forecast CLI rejects malformed requests", "[forecast-cli][validation]") {
    constexpr std::array<std::string_view, 0> missing_csv{};
    constexpr std::array zero_horizon{std::string_view{"--csv"},
                                      std::string_view{"prices.csv"},
                                      std::string_view{"--horizon-days"},
                                      std::string_view{"0"}};
    constexpr std::array bad_year{std::string_view{"--csv"},
                                  std::string_view{"prices.csv"},
                                  std::string_view{"--trading-days"},
                                  std::string_view{"nan"}};
    constexpr std::array duplicate{std::string_view{"--csv"},
                                   std::string_view{"one.csv"},
                                   std::string_view{"--csv"},
                                   std::string_view{"two.csv"}};
    constexpr std::array unknown{std::string_view{"--csv"},
                                 std::string_view{"prices.csv"},
                                 std::string_view{"--ticker"},
                                 std::string_view{"AAPL"}};
    constexpr std::array bad_model{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--volatility-model"}, std::string_view{"garch"}};
    constexpr std::array bad_decay{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--ewma-decay"}, std::string_view{"1"}};
    constexpr std::array unused_decay{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--ewma-decay"}, std::string_view{"0.90"}};

    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(missing_csv),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(zero_horizon),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(bad_year),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(duplicate),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(unknown),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(bad_model),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(bad_decay),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_forecast_options(unused_decay),
                      std::invalid_argument);
}
