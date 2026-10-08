#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/market_data.hpp"
#include "mc/models/black_scholes.hpp"
#include "mc/models/geometric_brownian_motion.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};

}

TEST_CASE("one GBM step agrees with terminal Black-Scholes evolution", "[path][model]") {
    constexpr double maturity = 1.25;
    constexpr double normal = -0.37;
    const mc::GeometricBrownianMotion path_model{kMarket, maturity, 1};
    const mc::BlackScholesModel terminal_model{kMarket, maturity};

    REQUIRE(path_model.initial_price() == kMarket.spot);
    REQUIRE(path_model.num_steps() == 1);
    REQUIRE(path_model.time_step() == maturity);
    REQUIRE(path_model.advance(path_model.initial_price(), normal) ==
            Catch::Approx(terminal_model.terminal_price(normal)));
    REQUIRE(path_model.discount_factor() ==
            Catch::Approx(terminal_model.discount_factor()));
}

TEST_CASE("expiry ignores extreme finite volatility", "[path][model]") {
    const mc::MarketData market{100.0, 0.05, 1e200};
    const mc::BlackScholesModel terminal{market, 0.0};
    const mc::GeometricBrownianMotion path{market, 0.0, 10};
    REQUIRE(terminal.terminal_price(1.0) == 100.0);
    REQUIRE(terminal.discount_factor() == 1.0);
    REQUIRE(path.advance(100.0, 1.0) == 100.0);
    REQUIRE(path.discount_factor() == 1.0);
    REQUIRE(path.num_steps() == 10);
}

TEST_CASE("models reject overflow before payoffs can mask it", "[path][model]") {
    const mc::BlackScholesModel terminal{kMarket, 1.0};
    const mc::GeometricBrownianMotion path{kMarket, 1.0, 1};
    REQUIRE_THROWS_AS(terminal.terminal_price(1e308), std::overflow_error);
    REQUIRE_THROWS_AS(path.advance(100.0, 1e308), std::overflow_error);
}

TEST_CASE("incremental GBM evolution matches the closed product of its steps",
          "[path][model]") {
    constexpr double maturity = 2.0;
    constexpr std::array normals{0.25, -1.0, 0.50, 1.25};
    const mc::GeometricBrownianMotion model{kMarket, maturity, normals.size()};
    double price = model.initial_price();
    double normal_sum = 0.0;

    for (const double normal : normals) {
        price = model.advance(price, normal);
        normal_sum += normal;
    }

    const double step = maturity / static_cast<double>(normals.size());
    const double expected =
        kMarket.spot *
        std::exp((kMarket.risk_free_rate -
                  0.5 * kMarket.volatility * kMarket.volatility) *
                     maturity +
                 kMarket.volatility * std::sqrt(step) * normal_sum);
    REQUIRE(price == Catch::Approx(expected).epsilon(1e-13));
}

TEST_CASE("GBM stepping supports deterministic and immediate-expiry paths",
          "[path][boundary]") {
    constexpr mc::MarketData deterministic{100.0, -0.02, 0.0};
    constexpr std::size_t steps = 252;
    const mc::GeometricBrownianMotion deterministic_model{deterministic, 1.5, steps};
    double deterministic_price = deterministic_model.initial_price();
    for (std::size_t step = 0; step < steps; ++step) {
        deterministic_price = deterministic_model.advance(deterministic_price, 99.0);
    }
    REQUIRE(deterministic_price ==
            Catch::Approx(deterministic.spot *
                          std::exp(deterministic.risk_free_rate * 1.5))
                .epsilon(1e-12));

    const mc::GeometricBrownianMotion expired_model{kMarket, 0.0, steps};
    double expired_price = expired_model.initial_price();
    for (std::size_t step = 0; step < steps; ++step) {
        expired_price = expired_model.advance(expired_price, -99.0);
    }
    REQUIRE(expired_price == kMarket.spot);
    REQUIRE(expired_model.discount_factor() == 1.0);
}

TEST_CASE("GBM path model rejects invalid construction", "[path][validation]") {
    auto invalid_market = kMarket;
    invalid_market.spot = 0.0;
    REQUIRE_THROWS_AS(mc::GeometricBrownianMotion(invalid_market, 1.0, 12),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::GeometricBrownianMotion(kMarket, -1.0, 12),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(
        mc::GeometricBrownianMotion(kMarket, std::numeric_limits<double>::infinity(), 12),
        std::invalid_argument);
    REQUIRE_THROWS_AS(mc::GeometricBrownianMotion(kMarket, 1.0, 0),
                      std::invalid_argument);

    auto overflowing_market = kMarket;
    overflowing_market.risk_free_rate = -std::numeric_limits<double>::max();
    REQUIRE_THROWS_AS(mc::BlackScholesModel(overflowing_market, 1.0),
                      std::overflow_error);
    REQUIRE_THROWS_AS(mc::GeometricBrownianMotion(overflowing_market, 1.0, 12),
                      std::overflow_error);
    REQUIRE_THROWS_AS(mc::BlackScholesModel(kMarket, -1.0),
                      std::invalid_argument);
}
