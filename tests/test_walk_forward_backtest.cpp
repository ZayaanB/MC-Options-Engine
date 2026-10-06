#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/forecasting/walk_forward_backtest.hpp"

TEST_CASE("walk-forward backtest never trains beyond its forecast origin",
          "[forecasting][backtest]") {
    using Catch::Approx;

    const std::array prices{100.0, 101.0, 102.01, 103.0301, 104.060401,
                            105.10100501, 106.1520150601, 107.213535210701,
                            108.285670562808, 109.368527268436,
                            110.462212541120};
    const mc::forecasting::BacktestConfig config{3, 2, 2, 252.0};
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(result.points.size() == 3);
    REQUIRE(result.points[0].origin_index == 3);
    REQUIRE(result.points[0].target_index == 5);
    REQUIRE(result.points[1].origin_index == 5);
    REQUIRE(result.points[1].target_index == 7);
    REQUIRE(result.points[2].origin_index == 7);
    REQUIRE(result.points[2].target_index == 9);
    REQUIRE(result.selected_model.mean_absolute_error ==
            Approx(0.0).margin(1e-10));
    REQUIRE(result.selected_model.root_mean_squared_error ==
            Approx(0.0).margin(1e-10));
    REQUIRE(result.latest_price.mean_absolute_error > 2.0);
    REQUIRE(result.selected_model.directional_accuracy == 1.0);
}

TEST_CASE("flat histories produce exact model and baseline forecasts",
          "[forecasting][backtest]") {
    using Catch::Approx;

    const std::array prices{100.0, 100.0, 100.0, 100.0, 100.0, 100.0};
    const mc::forecasting::BacktestConfig config{2, 1, 1, 252.0};
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(result.points.size() == 3);
    REQUIRE(result.selected_model.mean_absolute_error == 0.0);
    REQUIRE(result.selected_model.root_mean_squared_error == 0.0);
    REQUIRE(result.selected_model.mean_absolute_percentage_error == 0.0);
    REQUIRE(result.latest_price.mean_absolute_error == 0.0);
    REQUIRE(result.latest_price.root_mean_squared_error == 0.0);
    REQUIRE(result.latest_price.mean_absolute_percentage_error == 0.0);
    REQUIRE(result.selected_model.directional_predictions == 0);
    REQUIRE(std::isnan(result.selected_model.directional_accuracy));
    REQUIRE(result.interval_coverage == 1.0);
    REQUIRE(result.mean_interval_width == Approx(0.0).margin(1e-12));
}

TEST_CASE("future targets cannot leak into fitted forecasts",
          "[forecasting][backtest]") {
    using Catch::Approx;

    const std::array ordinary{100.0, 101.0, 102.0, 103.0};
    const std::array shocked{100.0, 101.0, 102.0, 500.0};
    const mc::forecasting::BacktestConfig config{2, 1, 1, 252.0};
    const auto ordinary_result =
        mc::forecasting::walk_forward_backtest(ordinary, config);
    const auto shocked_result = mc::forecasting::walk_forward_backtest(shocked, config);

    REQUIRE(ordinary_result.points.size() == 1);
    REQUIRE(shocked_result.points.size() == 1);
    REQUIRE(shocked_result.points.front().forecast_price ==
            Approx(ordinary_result.points.front().forecast_price));
    REQUIRE(shocked_result.points.front().lower_95 ==
            Approx(ordinary_result.points.front().lower_95));
    REQUIRE(shocked_result.points.front().upper_95 ==
            Approx(ordinary_result.points.front().upper_95));
    REQUIRE(shocked_result.points.front().historical_drift_forecast ==
            Approx(ordinary_result.points.front().historical_drift_forecast));
    REQUIRE(shocked_result.points.front().momentum_forecast ==
            Approx(ordinary_result.points.front().momentum_forecast));
    REQUIRE(shocked_result.points.front().mean_reversion_forecast ==
            Approx(ordinary_result.points.front().mean_reversion_forecast));
    REQUIRE(shocked_result.points.front().actual_price == 500.0);
}

TEST_CASE("backtest aggregates forecast errors and coverage",
          "[forecasting][backtest]") {
    const std::array prices{100.0, 102.0, 99.0, 103.0, 98.0,
                            104.0, 97.0, 105.0, 96.0, 106.0};
    const mc::forecasting::BacktestConfig config{3, 1, 1, 252.0};
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(result.points.size() == 6);
    REQUIRE(std::isfinite(result.selected_model.mean_absolute_error));
    REQUIRE(std::isfinite(result.selected_model.root_mean_squared_error));
    REQUIRE(std::isfinite(
        result.selected_model.mean_absolute_percentage_error));
    REQUIRE(result.selected_model.root_mean_squared_error >=
            result.selected_model.mean_absolute_error);
    REQUIRE(result.selected_model.directional_accuracy >= 0.0);
    REQUIRE(result.selected_model.directional_accuracy <= 1.0);
    REQUIRE(std::isfinite(result.momentum.mean_absolute_error));
    REQUIRE(std::isfinite(result.mean_reversion.mean_absolute_error));
    REQUIRE(result.interval_coverage >= 0.0);
    REQUIRE(result.interval_coverage <= 1.0);
    REQUIRE(result.mean_interval_width > 0.0);
}

TEST_CASE("zero-drift backtests use the latest price as the point forecast",
          "[forecasting][backtest][drift]") {
    using Catch::Approx;

    const std::array prices{100.0, 102.0, 99.0, 103.0, 98.0, 104.0};
    mc::forecasting::BacktestConfig config;
    config.lookback_days = 3;
    config.horizon_days = 1;
    config.step_days = 1;
    config.drift_estimator = mc::forecasting::DriftEstimator::zero;
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(result.points.size() == 2);
    for (const auto& point : result.points) {
        REQUIRE(point.forecast_price == Approx(point.current_price));
    }
    REQUIRE(result.selected_model.mean_absolute_error ==
            Approx(result.latest_price.mean_absolute_error));
    REQUIRE(result.selected_model.root_mean_squared_error ==
            Approx(result.latest_price.root_mean_squared_error));
    REQUIRE(result.selected_model.directional_predictions == 0);
}

TEST_CASE("benchmark forecasts use only trailing prices",
          "[forecasting][backtest][baseline]") {
    using Catch::Approx;

    const std::array prices{100.0, 110.0, 121.0, 133.1, 146.41, 161.051};
    const mc::forecasting::BacktestConfig config{3, 1, 1, 252.0};
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);
    const auto& first = result.points.front();

    REQUIRE(first.current_price == Approx(133.1));
    REQUIRE(first.latest_price_forecast == Approx(133.1));
    REQUIRE(first.zero_drift_forecast == Approx(133.1));
    REQUIRE(first.historical_drift_forecast == Approx(146.41));
    REQUIRE(first.momentum_forecast == Approx(146.41));
    REQUIRE(first.mean_reversion_forecast < first.current_price);
    REQUIRE(first.mean_reversion_forecast > prices.front());
}

TEST_CASE("backtest rejects invalid configurations and histories",
          "[forecasting][backtest][validation]") {
    const std::array prices{100.0, 101.0, 102.0, 103.0};
    const std::array invalid_prices{
        100.0, std::numeric_limits<double>::quiet_NaN(), 102.0, 103.0};

    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          prices, mc::forecasting::BacktestConfig{1, 1, 1, 252.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          prices, mc::forecasting::BacktestConfig{2, 0, 1, 252.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          prices, mc::forecasting::BacktestConfig{2, 1, 0, 252.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          prices, mc::forecasting::BacktestConfig{2, 2, 1, 252.0}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          invalid_prices,
                          mc::forecasting::BacktestConfig{2, 1, 1, 252.0}),
                      std::invalid_argument);
}
