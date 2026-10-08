#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/forecasting/walk_forward_backtest.hpp"
#include "mc/forecasting/price_history_csv.hpp"

TEST_CASE("scale-free errors and probability baselines use trailing training data", "[forecasting][backtest]") {
    const std::array prices{100.0, 110.0, 100.0, 120.0, 100.0, 130.0, 100.0};
    const auto result = mc::forecasting::walk_forward_backtest(
        prices, mc::forecasting::BacktestConfig{3, 2, 1, 252.0});
    REQUIRE(result.points.size() == 2);
    REQUIRE(result.points[0].naive_error_scale == Catch::Approx(40.0 / 3.0));
    REQUIRE(result.points[1].naive_error_scale == Catch::Approx(50.0 / 3.0));
    REQUIRE(result.points[0].historical_up_probability == 0.5);
    REQUIRE(result.points[1].historical_up_probability == 0.5);
    REQUIRE(result.probability.historical_up_brier_score == 0.25);
    REQUIRE(result.probability.half_brier_score == 0.25);
    REQUIRE(result.probability.always_up_brier_score == 0.5);
    REQUIRE(result.probability.always_up_accuracy == 0.5);
    double scaled_error = 0.0;
    for (const auto& point : result.points) {
        scaled_error += std::abs(point.forecast_price - point.actual_price) / point.naive_error_scale;
    }
    REQUIRE(result.selected_model.mean_absolute_scaled_error == Catch::Approx(scaled_error / 2.0));
    auto scaled_prices = prices;
    for (auto& price : scaled_prices) {
        price *= 5.0;
    }
    const auto scaled = mc::forecasting::walk_forward_backtest(
        scaled_prices, mc::forecasting::BacktestConfig{3, 2, 1, 252.0});
    REQUIRE(scaled.selected_model.mean_absolute_scaled_error ==
            Catch::Approx(result.selected_model.mean_absolute_scaled_error).epsilon(1e-10));
}

TEST_CASE("undefined scales and unavailable same-horizon history remain undefined", "[forecasting][backtest]") {
    const std::array flat{100.0, 100.0, 100.0, 100.0, 100.0, 100.0};
    const auto result = mc::forecasting::walk_forward_backtest(
        flat, mc::forecasting::BacktestConfig{2, 3, 1, 252.0});
    REQUIRE(std::isnan(result.selected_model.mean_absolute_scaled_error));
    REQUIRE(std::isnan(result.probability.historical_up_brier_score));
    REQUIRE(result.probability.always_up_accuracy == 0.0);
    REQUIRE(result.probability.always_up_brier_score == 1.0);
}

TEST_CASE("bootstrap block configuration and sensitivity preserve point forecasts", "[forecasting][backtest][bootstrap]") {
    std::array<double, 100> prices{};
    for (std::size_t index = 0; index < prices.size(); ++index) {
        prices[index] = 100.0 + static_cast<double>(index % 7);
    }
    mc::forecasting::BacktestConfig config{3, 2, 1, 252.0};
    config.bootstrap_samples = 100;
    const auto original = mc::forecasting::walk_forward_backtest(prices, config);
    config.bootstrap_block_size = 3;
    config.bootstrap_sensitivity = true;
    const auto result = mc::forecasting::walk_forward_backtest(prices, config);
    REQUIRE(result.mae_improvement.block_length == 3);
    REQUIRE(result.bootstrap_sensitivity.size() == 3);
    REQUIRE(result.bootstrap_sensitivity[0].block_length == 3);
    REQUIRE(result.bootstrap_sensitivity[1].block_length == 6);
    REQUIRE(result.bootstrap_sensitivity[2].block_length == 12);
    REQUIRE(std::isnan(result.bootstrap_sensitivity[2].lower_95));
    for (std::size_t index = 0; index < result.points.size(); ++index) {
        REQUIRE(result.points[index].forecast_price == original.points[index].forecast_price);
    }
    const auto repeated = mc::forecasting::walk_forward_backtest(prices, config);
    REQUIRE(repeated.mae_improvement.lower_95 == result.mae_improvement.lower_95);
    config.bootstrap_block_size = prices.size();
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(prices, config), std::invalid_argument);
}

TEST_CASE("explicit forecast lookbacks match walk-forward training at each origin", "[forecasting][backtest]") {
    const std::array prices{100.0, 101.0, 99.0, 103.0, 98.0, 104.0, 97.0, 105.0, 96.0};
    for (const auto estimator : {mc::forecasting::VolatilityEstimator::sample,
                                 mc::forecasting::VolatilityEstimator::ewma}) {
        mc::forecasting::BacktestConfig config{4, 2, 1, 252.0};
        config.volatility_estimator = estimator;
        const auto backtest = mc::forecasting::walk_forward_backtest(prices, config);
        for (const auto& point : backtest.points) {
            const auto available = std::span<const double>{prices}.first(point.origin_index + 1);
            const auto training = mc::forecasting::forecast_training_prices(available, 4);
            const auto model = mc::forecasting::estimate_gbm(training, estimator);
            const auto forecast = mc::forecasting::forecast_price(model, available.back(), 2);
            REQUIRE(forecast.expected_price == point.forecast_price);
            REQUIRE(forecast.lower_95 == point.lower_95);
            REQUIRE(model.return_observations == 4);
        }
    }
}

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
    REQUIRE(result.selected_model.directionally_correct == 3);
    REQUIRE(result.selected_model.directional_lower_95 > 0.0);
    REQUIRE(result.selected_model.directional_upper_95 == Approx(1.0));
    REQUIRE(std::isnan(result.mae_improvement.lower_95));
    REQUIRE(result.mae_improvement.conclusion ==
            mc::forecasting::ComparisonConclusion::inconclusive);
    REQUIRE(result.mae_improvement.block_length == 1);
    REQUIRE(result.probability.brier_score == 0.0);
    REQUIRE(result.probability.calibration.back().observations == 3);
    REQUIRE(result.probability.calibration.back().mean_forecast_probability ==
            1.0);
    REQUIRE(result.probability.calibration.back().observed_frequency == 1.0);
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
    REQUIRE(std::isnan(result.selected_model.directional_lower_95));
    REQUIRE(std::isnan(result.selected_model.directional_upper_95));
    REQUIRE(result.mae_improvement.absolute_improvement == 0.0);
    REQUIRE(std::isnan(result.mae_improvement.lower_95));
    REQUIRE(std::isnan(result.mae_improvement.upper_95));
    REQUIRE(result.mae_improvement.conclusion ==
            mc::forecasting::ComparisonConclusion::inconclusive);
    REQUIRE(result.interval_80.coverage == 1.0);
    REQUIRE(result.interval_90.coverage == 1.0);
    REQUIRE(result.interval_95.coverage == 1.0);
    REQUIRE(result.interval_95.mean_width == Approx(0.0).margin(1e-12));
    REQUIRE(result.interval_95.mean_interval_score ==
            Approx(0.0).margin(1e-12));
    REQUIRE(result.probability.brier_score == Approx(0.0));
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
    REQUIRE(shocked_result.interval_80.mean_interval_score >
            shocked_result.interval_80.mean_width);
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
    REQUIRE(result.interval_80.coverage >= 0.0);
    REQUIRE(result.interval_80.coverage <= 1.0);
    REQUIRE(result.interval_90.coverage >= 0.0);
    REQUIRE(result.interval_90.coverage <= 1.0);
    REQUIRE(result.interval_95.coverage >= 0.0);
    REQUIRE(result.interval_95.coverage <= 1.0);
    REQUIRE(result.interval_80.mean_width > 0.0);
    REQUIRE(result.interval_90.mean_width > result.interval_80.mean_width);
    REQUIRE(result.interval_95.mean_width > result.interval_90.mean_width);
    REQUIRE(result.interval_80.mean_interval_score > 0.0);
    REQUIRE(result.probability.brier_score >= 0.0);
    REQUIRE(result.probability.brier_score <= 1.0);
    REQUIRE(result.probability.calibration.size() == 5);
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
    REQUIRE(std::isnan(result.mae_improvement.lower_95));
    REQUIRE(std::isnan(result.mae_improvement.upper_95));
    REQUIRE(result.mae_improvement.conclusion ==
            mc::forecasting::ComparisonConclusion::inconclusive);
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

TEST_CASE("paired bootstrap is deterministic and preserves overlapping blocks",
          "[forecasting][backtest][bootstrap]") {
    using Catch::Approx;

    std::array<double, 64> prices{};
    for (std::size_t index = 0; index < prices.size(); ++index) {
        prices[index] = 100.0 + static_cast<double>(index % 7);
    }
    mc::forecasting::BacktestConfig config;
    config.lookback_days = 3;
    config.horizon_days = 2;
    config.step_days = 1;
    config.bootstrap_samples = 500;
    config.bootstrap_seed = 1234;

    const auto first = mc::forecasting::walk_forward_backtest(prices, config);
    const auto second = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(first.mae_improvement.lower_95 ==
            Approx(second.mae_improvement.lower_95));
    REQUIRE(first.mae_improvement.upper_95 ==
            Approx(second.mae_improvement.upper_95));
    REQUIRE(first.mae_improvement.block_length == 2);
    REQUIRE(first.mae_improvement.bootstrap_samples == 500);
    REQUIRE(first.mae_improvement.bootstrap_seed == 1234);
}

TEST_CASE("paired bootstrap does not infer significance from one forecast",
          "[forecasting][backtest][bootstrap]") {
    const std::array prices{100.0, 110.0, 121.0, 133.1, 133.1};
    mc::forecasting::BacktestConfig config;
    config.lookback_days = 3;
    config.horizon_days = 1;
    config.bootstrap_samples = 100;

    const auto result = mc::forecasting::walk_forward_backtest(prices, config);

    REQUIRE(std::isnan(result.mae_improvement.upper_95));
    REQUIRE(result.mae_improvement.conclusion ==
            mc::forecasting::ComparisonConclusion::inconclusive);
}

TEST_CASE("backtest rejects invalid configurations and histories",
          "[forecasting][backtest][validation]") {
    const std::array prices{100.0, 101.0, 102.0, 103.0};
    const std::array invalid_prices{
        100.0, std::numeric_limits<double>::quiet_NaN(), 102.0, 103.0};
    auto invalid_bootstrap = mc::forecasting::BacktestConfig{2, 1, 1, 252.0};
    invalid_bootstrap.bootstrap_samples = 99;

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
    REQUIRE_THROWS_AS(mc::forecasting::walk_forward_backtest(
                          prices, invalid_bootstrap),
                      std::invalid_argument);
}
