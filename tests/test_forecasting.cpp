#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/forecasting/historical_gbm.hpp"

TEST_CASE("historical GBM estimates log-return parameters", "[forecasting]") {
    using Catch::Approx;

    const std::array closes{100.0, 110.0, 99.0, 108.9};
    const auto model = mc::forecasting::estimate_historical_gbm(closes, 252.0);
    const double up = std::log(1.1);
    const double down = std::log(0.9);
    const double mean = (2.0 * up + down) / 3.0;
    const double variance =
        (2.0 * (up - mean) * (up - mean) + (down - mean) * (down - mean)) / 2.0;

    REQUIRE(model.return_observations == 3);
    REQUIRE(model.mean_daily_log_return == Approx(mean));
    REQUIRE(model.daily_volatility == Approx(std::sqrt(variance)));
    REQUIRE(model.annualized_volatility == Approx(std::sqrt(variance * 252.0)));
    REQUIRE(model.annualized_drift ==
            Approx(mean * 252.0 + 0.5 * variance * 252.0));
}

TEST_CASE("forecast reports the moments and interval of its lognormal model",
          "[forecasting]") {
    using Catch::Approx;

    const mc::forecasting::HistoricalGbmModel model{
        100, 0.001, 0.02, 0.0, 0.02 * std::sqrt(252.0), 252.0};
    const auto result = mc::forecasting::forecast_price(model, 100.0, 25);
    const double log_mean = 0.025;
    const double log_standard_deviation = 0.1;

    REQUIRE(result.current_price == 100.0);
    REQUIRE(result.horizon_days == 25);
    REQUIRE(result.median_price == Approx(100.0 * std::exp(log_mean)));
    REQUIRE(result.expected_price ==
            Approx(100.0 * std::exp(log_mean + 0.5 * log_standard_deviation *
                                                   log_standard_deviation)));
    REQUIRE(result.lower_95 == Approx(100.0 * std::exp(log_mean -
                                                       1.959963984540054 *
                                                           log_standard_deviation)));
    REQUIRE(result.upper_95 == Approx(100.0 * std::exp(log_mean +
                                                       1.959963984540054 *
                                                           log_standard_deviation)));
    REQUIRE(result.probability_above_current == Approx(0.5987063257));
}

TEST_CASE("deterministic historical returns produce deterministic forecasts",
          "[forecasting]") {
    using Catch::Approx;

    const std::array closes{100.0, 110.0, 121.0, 133.1};
    const auto model = mc::forecasting::estimate_historical_gbm(closes);
    const auto result = mc::forecasting::forecast_price(model, 133.1, 2);

    REQUIRE(model.daily_volatility == Approx(0.0).margin(1e-15));
    REQUIRE(result.expected_price == Approx(161.051));
    REQUIRE(result.median_price == Approx(161.051));
    REQUIRE(result.lower_95 == Approx(161.051));
    REQUIRE(result.upper_95 == Approx(161.051));
    REQUIRE(result.probability_above_current == 1.0);
}

TEST_CASE("forecasting rejects insufficient or invalid market history",
          "[forecasting][validation]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    REQUIRE_THROWS_AS(mc::forecasting::estimate_historical_gbm(
                          std::array{100.0, 101.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::estimate_historical_gbm(
                          std::array{100.0, 0.0, 101.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::estimate_historical_gbm(
                          std::array{100.0, nan, 101.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::estimate_historical_gbm(
                          std::array{100.0, 101.0, 102.0}, 0.0),
                      std::invalid_argument);
}

TEST_CASE("forecasting rejects invalid model requests", "[forecasting][validation]") {
    const mc::forecasting::HistoricalGbmModel valid{
        10, 0.001, 0.02, 0.0, 0.02 * std::sqrt(252.0), 252.0};
    auto invalid = valid;
    invalid.daily_volatility = -0.1;

    REQUIRE_THROWS_AS(mc::forecasting::forecast_price(valid, 0.0, 10),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::forecast_price(valid, 100.0, 0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::forecast_price(invalid, 100.0, 10),
                      std::invalid_argument);
}
