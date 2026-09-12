#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"

namespace {

constexpr double kSpot = 100.0;
constexpr double kStrike = 100.0;
constexpr double kRate = 0.05;
constexpr double kVolatility = 0.20;
constexpr double kMaturity = 1.0;

}  // namespace

TEST_CASE("standard normal CDF has expected values and symmetry", "[black-scholes][cdf]") {
    using Catch::Approx;

    REQUIRE(mc::standard_normal_cdf(0.0) == Approx(0.5));
    REQUIRE(mc::standard_normal_cdf(1.0) == Approx(0.8413447460685429).epsilon(1e-14));

    for (const double value : std::array{-3.0, -0.5, 0.5, 3.0}) {
        REQUIRE(mc::standard_normal_cdf(value) + mc::standard_normal_cdf(-value) ==
                Approx(1.0).epsilon(1e-14));
    }
}

TEST_CASE("analytical Black-Scholes matches the canonical European prices",
          "[black-scholes]") {
    using Catch::Approx;

    REQUIRE(mc::black_scholes_call(kSpot, kStrike, kRate, kVolatility, kMaturity) ==
            Approx(10.450583572185565).epsilon(1e-12));
    REQUIRE(mc::black_scholes_put(kSpot, kStrike, kRate, kVolatility, kMaturity) ==
            Approx(5.573526022256971).epsilon(1e-12));
}

TEST_CASE("domain object overloads match scalar Black-Scholes inputs", "[black-scholes]") {
    using Catch::Approx;

    constexpr mc::MarketData market{
        .spot = kSpot,
        .risk_free_rate = kRate,
        .volatility = kVolatility,
    };
    constexpr mc::OptionParameters option{
        .strike = kStrike,
        .maturity = kMaturity,
    };

    REQUIRE(mc::black_scholes_call(market, option) ==
            Approx(mc::black_scholes_call(kSpot, kStrike, kRate, kVolatility, kMaturity)));
    REQUIRE(mc::black_scholes_put(market, option) ==
            Approx(mc::black_scholes_put(kSpot, kStrike, kRate, kVolatility, kMaturity)));
}

TEST_CASE("analytical prices satisfy put-call parity with negative rates",
          "[black-scholes][parity]") {
    using Catch::Approx;

    constexpr double rate = -0.01;
    constexpr double maturity = 2.0;
    constexpr double strike = 105.0;

    const double call = mc::black_scholes_call(100.0, strike, rate, 0.25, maturity);
    const double put = mc::black_scholes_put(100.0, strike, rate, 0.25, maturity);

    REQUIRE(call - put == Approx(100.0 - strike * std::exp(-rate * maturity)).epsilon(1e-12));
}

TEST_CASE("zero maturity returns immediate intrinsic value", "[black-scholes][boundary]") {
    REQUIRE(mc::black_scholes_call(110.0, 100.0, 0.05, 0.20, 0.0) == 10.0);
    REQUIRE(mc::black_scholes_call(90.0, 100.0, 0.05, 0.20, 0.0) == 0.0);
    REQUIRE(mc::black_scholes_put(90.0, 100.0, 0.05, 0.20, 0.0) == 10.0);
    REQUIRE(mc::black_scholes_put(110.0, 100.0, 0.05, 0.20, 0.0) == 0.0);
}

TEST_CASE("zero volatility returns discounted deterministic payoff",
          "[black-scholes][boundary]") {
    using Catch::Approx;

    const double discounted_strike = kStrike * std::exp(-kRate * kMaturity);

    REQUIRE(mc::black_scholes_call(kSpot, kStrike, kRate, 0.0, kMaturity) ==
            Approx(std::max(kSpot - discounted_strike, 0.0)));
    REQUIRE(mc::black_scholes_put(kSpot, kStrike, kRate, 0.0, kMaturity) ==
            Approx(std::max(discounted_strike - kSpot, 0.0)));
}

TEST_CASE("analytical pricing rejects invalid inputs", "[black-scholes][validation]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();

    REQUIRE_THROWS_AS(mc::black_scholes_call(0.0, 100.0, 0.05, 0.20, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::black_scholes_put(100.0, -1.0, 0.05, 0.20, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::black_scholes_call(100.0, 100.0, 0.05, -0.20, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::black_scholes_put(100.0, 100.0, 0.05, 0.20, -1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::black_scholes_call(nan, 100.0, 0.05, 0.20, 1.0),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::black_scholes_put(100.0, 100.0, infinity, 0.20, 1.0),
                      std::invalid_argument);
}
