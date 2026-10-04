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

TEST_CASE("EWMA volatility gives recent deviations more weight", "[forecasting][ewma]") {
    using Catch::Approx;

    const std::array closes{100.0, 110.0, 99.0, 108.9};
    const double decay = 0.5;
    const auto model = mc::forecasting::estimate_ewma_gbm(closes, decay, 252.0);
    const double up = std::log(1.1);
    const double down = std::log(0.9);
    const double mean = (2.0 * up + down) / 3.0;
    const double up_squared = (up - mean) * (up - mean);
    const double down_squared = (down - mean) * (down - mean);
    const double expected_variance =
        (1.25 * up_squared + 0.5 * down_squared) / 1.75;

    REQUIRE(model.return_observations == 3);
    REQUIRE(model.mean_daily_log_return == Approx(mean));
    REQUIRE(model.daily_volatility == Approx(std::sqrt(expected_variance)));
    REQUIRE(model.annualized_volatility ==
            Approx(std::sqrt(expected_variance * 252.0)));
}

TEST_CASE("EWMA reacts more strongly to a recent return shock",
          "[forecasting][ewma]") {
    using Catch::Approx;

    const std::array old_shock{100.0, 110.0, 110.0, 110.0, 110.0};
    const std::array recent_shock{100.0, 100.0, 100.0, 100.0, 110.0};

    const auto old_model = mc::forecasting::estimate_ewma_gbm(old_shock, 0.8);
    const auto recent_model = mc::forecasting::estimate_ewma_gbm(recent_shock, 0.8);

    REQUIRE(recent_model.mean_daily_log_return ==
            Approx(old_model.mean_daily_log_return));
    REQUIRE(recent_model.daily_volatility > old_model.daily_volatility);
}

TEST_CASE("GBM estimator dispatch selects sample or EWMA volatility",
          "[forecasting][ewma]") {
    using Catch::Approx;

    const std::array closes{100.0, 103.0, 101.0, 105.0, 102.0};
    const auto sample = mc::forecasting::estimate_historical_gbm(closes);
    const auto selected_sample = mc::forecasting::estimate_gbm(
        closes, mc::forecasting::VolatilityEstimator::sample);
    const auto ewma = mc::forecasting::estimate_ewma_gbm(closes, 0.8);
    const auto selected_ewma = mc::forecasting::estimate_gbm(
        closes, mc::forecasting::VolatilityEstimator::ewma, 0.8);

    REQUIRE(selected_sample.daily_volatility == Approx(sample.daily_volatility));
    REQUIRE(selected_ewma.daily_volatility == Approx(ewma.daily_volatility));
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
    REQUIRE_THROWS_AS(mc::forecasting::estimate_ewma_gbm(
                          std::array{100.0, 101.0, 102.0}, 0.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::estimate_ewma_gbm(
                          std::array{100.0, 101.0, 102.0}, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::estimate_ewma_gbm(
                          std::array{100.0, 101.0, 102.0}, nan),
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
