#include <memory>
#include <limits>
#include <stdexcept>

#include <catch2/catch_test_macros.hpp>

#include "mc/instruments/european_call.hpp"
#include "mc/instruments/european_put.hpp"
#include "mc/instruments/instrument.hpp"
#include "mc/instruments/arithmetic_asian_call.hpp"

TEST_CASE("European call payoff is positive only above the strike", "[payoff][call]") {
    const mc::EuropeanCall call{100.0};

    REQUIRE(call.payoff(120.0) == 20.0);
    REQUIRE(call.payoff(100.0) == 0.0);
    REQUIRE(call.payoff(80.0) == 0.0);
    REQUIRE(call.strike() == 100.0);
}

TEST_CASE("European put payoff is positive only below the strike", "[payoff][put]") {
    const mc::EuropeanPut put{100.0};

    REQUIRE(put.payoff(80.0) == 20.0);
    REQUIRE(put.payoff(100.0) == 0.0);
    REQUIRE(put.payoff(120.0) == 0.0);
    REQUIRE(put.strike() == 100.0);
}

TEST_CASE("European instruments support payoff polymorphism", "[payoff][instrument]") {
    const std::unique_ptr<mc::Instrument> instrument =
        std::make_unique<mc::EuropeanCall>(100.0);

    REQUIRE(instrument->payoff(112.5) == 12.5);
}

TEST_CASE("instruments reject invalid strikes", "[payoff][validation]") {
    const double nan = std::numeric_limits<double>::quiet_NaN();

    REQUIRE_THROWS_AS(mc::EuropeanCall{0.0}, std::invalid_argument);
    REQUIRE_THROWS_AS(mc::EuropeanPut{-1.0}, std::invalid_argument);
    REQUIRE_THROWS_AS(mc::ArithmeticAsianCall{nan}, std::invalid_argument);
}
