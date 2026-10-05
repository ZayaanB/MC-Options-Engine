#include <array>
#include <stdexcept>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

#include "mc/cli/backtest_options.hpp"

TEST_CASE("backtest CLI parses rolling evaluation options", "[backtest-cli]") {
    constexpr std::array arguments{
        std::string_view{"--csv"}, std::string_view{"aapl.csv"},
        std::string_view{"--price-column"}, std::string_view{"Close"},
        std::string_view{"--lookback-days"}, std::string_view{"126"},
        std::string_view{"--horizon-days"}, std::string_view{"10"},
        std::string_view{"--step-days"}, std::string_view{"5"},
        std::string_view{"--trading-days"}, std::string_view{"250"},
        std::string_view{"--volatility-model"}, std::string_view{"ewma"},
        std::string_view{"--ewma-decay"}, std::string_view{"0.97"},
        std::string_view{"--drift-model"}, std::string_view{"shrinkage"},
        std::string_view{"--drift-shrinkage"}, std::string_view{"0.25"},
    };
    const auto options = mc::cli::parse_backtest_options(arguments);

    REQUIRE(options.csv_path == "aapl.csv");
    REQUIRE(options.price_column == "Close");
    REQUIRE(options.config.lookback_days == 126);
    REQUIRE(options.config.horizon_days == 10);
    REQUIRE(options.config.step_days == 5);
    REQUIRE(options.config.trading_days_per_year == 250.0);
    REQUIRE(options.config.volatility_estimator ==
            mc::forecasting::VolatilityEstimator::ewma);
    REQUIRE(options.config.ewma_decay == 0.97);
    REQUIRE(options.config.drift_estimator ==
            mc::forecasting::DriftEstimator::shrinkage);
    REQUIRE(options.config.drift_shrinkage == 0.25);
}

TEST_CASE("backtest CLI uses rolling defaults", "[backtest-cli]") {
    constexpr std::array arguments{std::string_view{"--csv"},
                                   std::string_view{"prices.csv"}};
    const auto options = mc::cli::parse_backtest_options(arguments);

    REQUIRE(options.price_column == "Adj Close");
    REQUIRE(options.config.lookback_days == 252);
    REQUIRE(options.config.horizon_days == 20);
    REQUIRE(options.config.step_days == 1);
    REQUIRE(options.config.trading_days_per_year == 252.0);
    REQUIRE(options.config.volatility_estimator ==
            mc::forecasting::VolatilityEstimator::sample);
    REQUIRE(options.config.ewma_decay == 0.94);
    REQUIRE(options.config.drift_estimator ==
            mc::forecasting::DriftEstimator::historical);
    REQUIRE(options.config.drift_shrinkage == 0.5);
}

TEST_CASE("backtest CLI rejects malformed requests", "[backtest-cli][validation]") {
    constexpr std::array<std::string_view, 0> missing_csv{};
    constexpr std::array zero_lookback{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--lookback-days"}, std::string_view{"0"}};
    constexpr std::array bad_step{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--step-days"}, std::string_view{"1.5"}};
    constexpr std::array duplicate{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--horizon-days"}, std::string_view{"5"},
        std::string_view{"--horizon-days"}, std::string_view{"10"}};
    constexpr std::array bad_model{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--volatility-model"}, std::string_view{"garch"}};
    constexpr std::array unused_decay{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--ewma-decay"}, std::string_view{"0.90"}};
    constexpr std::array bad_drift{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--drift-model"}, std::string_view{"random"}};
    constexpr std::array unused_shrinkage{
        std::string_view{"--csv"}, std::string_view{"prices.csv"},
        std::string_view{"--drift-shrinkage"}, std::string_view{"0.5"}};

    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(missing_csv),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(zero_lookback),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(bad_step),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(duplicate),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(bad_model),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(unused_decay),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(bad_drift),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::cli::parse_backtest_options(unused_shrinkage),
                      std::invalid_argument);
}
