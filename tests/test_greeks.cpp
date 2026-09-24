#include <cmath>
#include <limits>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/greeks_config.hpp"
#include "mc/instruments/european_call.hpp"
#include "mc/instruments/european_put.hpp"
#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/greeks_engine.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"
#include "mc/statistics/running_statistics.hpp"

namespace {

constexpr mc::MarketData kMarket{100.0, 0.05, 0.20};
constexpr mc::OptionParameters kOption{100.0, 1.0};
constexpr mc::SimulationConfig kSimulation{500'000, 42, 1, 0, false};
constexpr double kPi = 3.14159265358979323846;

struct AnalyticalGreeks {
    double call_delta;
    double put_delta;
    double gamma;
    double vega_per_percentage_point;
};

AnalyticalGreeks analytical_greeks() {
    const double root_time = std::sqrt(kOption.maturity);
    const double d1 =
        (std::log(kMarket.spot / kOption.strike) +
         (kMarket.risk_free_rate + 0.5 * kMarket.volatility * kMarket.volatility) *
             kOption.maturity) /
        (kMarket.volatility * root_time);
    const double density = std::exp(-0.5 * d1 * d1) / std::sqrt(2.0 * kPi);
    return {
        .call_delta = mc::standard_normal_cdf(d1),
        .put_delta = mc::standard_normal_cdf(d1) - 1.0,
        .gamma = density / (kMarket.spot * kMarket.volatility * root_time),
        .vega_per_percentage_point = kMarket.spot * density * root_time * 0.01,
    };
}

}  // namespace

TEST_CASE("finite-difference call Greeks agree with analytical Black-Scholes Greeks",
          "[greeks]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::GreeksEngine engine;
    const auto result = engine.calculate(call, kMarket, kOption, kSimulation);
    const auto expected = analytical_greeks();

    REQUIRE(result.delta == Catch::Approx(expected.call_delta).margin(0.005));
    REQUIRE(result.gamma == Catch::Approx(expected.gamma).margin(0.001));
    REQUIRE(result.vega ==
            Catch::Approx(expected.vega_per_percentage_point).margin(0.005));
}

TEST_CASE("finite-difference put Greeks agree with analytical Black-Scholes Greeks",
          "[greeks]") {
    const mc::EuropeanPut put{kOption.strike};
    const mc::GreeksEngine engine;
    const auto result = engine.calculate(put, kMarket, kOption, kSimulation);
    const auto expected = analytical_greeks();

    REQUIRE(result.delta == Catch::Approx(expected.put_delta).margin(0.005));
    REQUIRE(result.gamma == Catch::Approx(expected.gamma).margin(0.001));
    REQUIRE(result.vega ==
            Catch::Approx(expected.vega_per_percentage_point).margin(0.005));
}

TEST_CASE("Greek bumps are configurable and fixed configurations are reproducible",
          "[greeks][rng]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::GreeksEngine engine;
    const mc::GreeksConfig config{.spot_bump = 0.5, .volatility_bump = 0.005};

    const auto first = engine.calculate(call, kMarket, kOption, kSimulation, config);
    const auto second = engine.calculate(call, kMarket, kOption, kSimulation, config);

    REQUIRE(first.delta == second.delta);
    REQUIRE(first.gamma == second.gamma);
    REQUIRE(first.vega == second.vega);
    REQUIRE(first.delta == Catch::Approx(analytical_greeks().call_delta).margin(0.005));
}

TEST_CASE("Greeks engine rejects invalid central-difference bumps", "[greeks][validation]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::GreeksEngine engine;

    REQUIRE_THROWS_AS(engine.calculate(call, kMarket, kOption, kSimulation,
                                       {.spot_bump = 0.0, .volatility_bump = 0.01}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(engine.calculate(call, kMarket, kOption, kSimulation,
                                       {.spot_bump = kMarket.spot,
                                        .volatility_bump = 0.01}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(engine.calculate(call, kMarket, kOption, kSimulation,
                                       {.spot_bump = 1.0, .volatility_bump = 0.21}),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(
        engine.calculate(call, kMarket, kOption, kSimulation,
                         {.spot_bump = 1.0,
                          .volatility_bump = std::numeric_limits<double>::infinity()}),
        std::invalid_argument);
}

TEST_CASE("common random numbers reduce Monte Carlo Delta variance", "[greeks][rng]") {
    constexpr std::uint64_t replications = 40;
    constexpr std::uint64_t paths = 10'000;
    constexpr double bump = 1.0;
    constexpr std::uint64_t independent_offset = 0x9e3779b97f4a7c15ULL;
    const mc::EuropeanCall call{kOption.strike};
    const mc::GreeksEngine greeks_engine;
    const mc::MonteCarloEngine pricing_engine;
    mc::RunningStatistics common_deltas;
    mc::RunningStatistics independent_deltas;

    for (std::uint64_t replication = 0; replication < replications; ++replication) {
        const std::uint64_t seed = 1'000 + replication;
        const mc::SimulationConfig simulation{paths, seed, 1, 0, false};
        common_deltas.add(
            greeks_engine
                .calculate(call, kMarket, kOption, simulation,
                           {.spot_bump = bump, .volatility_bump = 0.01})
                .delta);

        const mc::MarketData up{kMarket.spot + bump, kMarket.risk_free_rate,
                                kMarket.volatility};
        const mc::MarketData down{kMarket.spot - bump, kMarket.risk_free_rate,
                                  kMarket.volatility};
        auto down_simulation = simulation;
        down_simulation.seed += independent_offset;
        const double up_price = pricing_engine.price(call, up, kOption, simulation).price;
        const double down_price = pricing_engine.price(call, down, kOption, down_simulation).price;
        independent_deltas.add((up_price - down_price) / (2.0 * bump));
    }

    REQUIRE(common_deltas.variance() < independent_deltas.variance());
    REQUIRE(independent_deltas.variance() / common_deltas.variance() > 20.0);
}
