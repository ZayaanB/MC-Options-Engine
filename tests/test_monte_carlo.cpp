#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stdexcept>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/instruments/european_call.hpp"
#include "mc/instruments/european_put.hpp"
#include "mc/market_data.hpp"
#include "mc/models/black_scholes.hpp"
#include "mc/option_parameters.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/simulation_config.hpp"

namespace {

constexpr mc::MarketData kMarket{
    .spot = 100.0,
    .risk_free_rate = 0.05,
    .volatility = 0.20,
};

constexpr mc::OptionParameters kOption{
    .strike = 100.0,
    .maturity = 1.0,
};

constexpr mc::SimulationConfig simulation_config(const std::uint64_t paths = 100'000) {
    return {
        .num_paths = paths,
        .seed = 42,
        .num_threads = 1,
        .batch_size = 0,
        .antithetic = false,
    };
}

}  // namespace

TEST_CASE("Black-Scholes model evolves GBM and supplies discounting", "[model]") {
    using Catch::Approx;

    const mc::BlackScholesModel model{kMarket, kOption.maturity};
    const double expected_at_zero =
        kMarket.spot *
        std::exp((kMarket.risk_free_rate - 0.5 * kMarket.volatility * kMarket.volatility) *
                 kOption.maturity);

    REQUIRE(model.terminal_price(0.0) == Approx(expected_at_zero));
    REQUIRE(model.discount_factor() ==
            Approx(std::exp(-kMarket.risk_free_rate * kOption.maturity)));
}

TEST_CASE("single-threaded Monte Carlo call agrees with Black-Scholes statistically",
          "[monte-carlo]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = simulation_config(250'000);

    const mc::PricingResult result = engine.price(call, kMarket, kOption, config);
    const double analytical = mc::black_scholes_call(kMarket, kOption);

    REQUIRE(std::abs(result.price - analytical) < 3.0 * result.standard_error);
    REQUIRE(result.confidence_lower ==
            Catch::Approx(result.price - 1.96 * result.standard_error));
    REQUIRE(result.confidence_upper ==
            Catch::Approx(result.price + 1.96 * result.standard_error));
    REQUIRE(result.paths == config.num_paths);
    REQUIRE(result.observations == config.num_paths);
    REQUIRE(result.sample_variance > 0.0);
    REQUIRE(result.standard_error > 0.0);
    REQUIRE(result.runtime_seconds > 0.0);
    REQUIRE(result.paths_per_second > 0.0);
}

TEST_CASE("single-threaded Monte Carlo put agrees with Black-Scholes statistically",
          "[monte-carlo]") {
    const mc::EuropeanPut put{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = simulation_config(250'000);

    const mc::PricingResult result = engine.price(put, kMarket, kOption, config);
    const double analytical = mc::black_scholes_put(kMarket, kOption);

    REQUIRE(std::abs(result.price - analytical) < 3.0 * result.standard_error);
    REQUIRE(result.confidence_lower < result.price);
    REQUIRE(result.confidence_upper > result.price);
    REQUIRE(result.paths == config.num_paths);
}

TEST_CASE("Monte Carlo call and put estimates satisfy put-call parity statistically",
          "[monte-carlo][parity]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::EuropeanPut put{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = simulation_config(250'000);

    const mc::PricingResult call_result = engine.price(call, kMarket, kOption, config);
    const mc::PricingResult put_result = engine.price(put, kMarket, kOption, config);
    const double expected_difference =
        kMarket.spot - kOption.strike * std::exp(-kMarket.risk_free_rate * kOption.maturity);
    const double conservative_error =
        3.0 * (call_result.standard_error + put_result.standard_error);

    REQUIRE(std::abs((call_result.price - put_result.price) - expected_difference) <
            conservative_error);
}

TEST_CASE("fixed full configuration reproduces Monte Carlo estimates", "[monte-carlo][rng]") {
    const mc::EuropeanPut put{kOption.strike};
    const mc::MonteCarloEngine engine;
    const auto config = simulation_config(10'000);

    const mc::PricingResult first = engine.price(put, kMarket, kOption, config);
    const mc::PricingResult second = engine.price(put, kMarket, kOption, config);

    REQUIRE(first.price == second.price);
    REQUIRE(first.sample_variance == second.sample_variance);
    REQUIRE(first.standard_error == second.standard_error);
    REQUIRE(first.confidence_lower == second.confidence_lower);
    REQUIRE(first.confidence_upper == second.confidence_upper);
}

TEST_CASE("deterministic market produces exact discounted payoff", "[monte-carlo][boundary]") {
    using Catch::Approx;

    constexpr mc::MarketData deterministic_market{
        .spot = 100.0,
        .risk_free_rate = 0.05,
        .volatility = 0.0,
    };
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;

    const mc::PricingResult result =
        engine.price(call, deterministic_market, kOption, simulation_config(100));

    REQUIRE(result.price ==
            Approx(mc::black_scholes_call(deterministic_market, kOption)).epsilon(1e-12));
    REQUIRE(result.sample_variance == Approx(0.0).margin(1e-25));
    REQUIRE(result.standard_error == Approx(0.0).margin(1e-25));
}

TEST_CASE("zero maturity returns immediate payoff", "[monte-carlo][boundary]") {
    using Catch::Approx;

    constexpr mc::OptionParameters expired_option{
        .strike = 90.0,
        .maturity = 0.0,
    };
    const mc::EuropeanCall call{expired_option.strike};
    const mc::MonteCarloEngine engine;

    const mc::PricingResult result =
        engine.price(call, kMarket, expired_option, simulation_config(100));

    REQUIRE(result.price == Approx(10.0));
    REQUIRE(result.sample_variance == Approx(0.0).margin(1e-25));
    REQUIRE(result.standard_error == Approx(0.0).margin(1e-25));
}

TEST_CASE("one path returns a price and undefined uncertainty", "[monte-carlo][boundary]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;

    const mc::PricingResult result = engine.price(call, kMarket, kOption, simulation_config(1));

    REQUIRE(std::isfinite(result.price));
    REQUIRE(std::isnan(result.sample_variance));
    REQUIRE(std::isnan(result.standard_error));
    REQUIRE(std::isnan(result.confidence_lower));
    REQUIRE(std::isnan(result.confidence_upper));
}

TEST_CASE("antithetic pairs use averaged payoffs as independent observations",
          "[monte-carlo][antithetic]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    auto config = simulation_config(4);
    config.antithetic = true;

    std::mt19937_64 rng{config.seed};
    std::normal_distribution<double> normal{0.0, 1.0};
    const mc::BlackScholesModel model{kMarket, kOption.maturity};
    const auto paired_payoff = [&] {
        const double z = normal(rng);
        return model.discount_factor() * 0.5 *
               (call.payoff(model.terminal_price(z)) +
                call.payoff(model.terminal_price(-z)));
    };
    const double first = paired_payoff();
    const double second = paired_payoff();
    const auto result = engine.price(call, kMarket, kOption, config);

    REQUIRE(result.paths == 4);
    REQUIRE(result.observations == 2);
    REQUIRE(result.price == Catch::Approx((first + second) / 2.0));
    REQUIRE(result.sample_variance ==
            Catch::Approx((first - second) * (first - second) / 2.0));
    REQUIRE(result.standard_error ==
            Catch::Approx(std::sqrt(result.sample_variance / 2.0)));
}

TEST_CASE("one antithetic pair has undefined uncertainty", "[monte-carlo][antithetic]") {
    const mc::EuropeanPut put{kOption.strike};
    const mc::MonteCarloEngine engine;
    auto config = simulation_config(2);
    config.antithetic = true;
    const auto result = engine.price(put, kMarket, kOption, config);

    REQUIRE(result.paths == 2);
    REQUIRE(result.observations == 1);
    REQUIRE(std::isfinite(result.price));
    REQUIRE(std::isnan(result.sample_variance));
    REQUIRE(std::isnan(result.standard_error));
    REQUIRE(std::isnan(result.confidence_lower));
    REQUIRE(std::isnan(result.confidence_upper));
}

TEST_CASE("antithetic call and put agree with Black-Scholes statistically",
          "[monte-carlo][antithetic]") {
    const mc::MonteCarloEngine engine;
    auto config = simulation_config(250'000);
    config.antithetic = true;
    const mc::EuropeanCall call{kOption.strike};
    const mc::EuropeanPut put{kOption.strike};

    const auto call_result = engine.price(call, kMarket, kOption, config);
    const auto put_result = engine.price(put, kMarket, kOption, config);

    REQUIRE(call_result.observations == config.num_paths / 2);
    REQUIRE(put_result.observations == config.num_paths / 2);
    REQUIRE(std::abs(call_result.price - mc::black_scholes_call(kMarket, kOption)) <
            4.0 * call_result.standard_error);
    REQUIRE(std::abs(put_result.price - mc::black_scholes_put(kMarket, kOption)) <
            4.0 * put_result.standard_error);
}

TEST_CASE("antithetic pairing reduces call estimator variance at equal path counts",
          "[monte-carlo][antithetic]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;
    auto config = simulation_config(250'000);
    const auto standard = engine.price(call, kMarket, kOption, config);
    config.antithetic = true;
    const auto paired = engine.price(call, kMarket, kOption, config);

    REQUIRE(standard.paths == paired.paths);
    REQUIRE(standard.observations == 2 * paired.observations);
    REQUIRE(paired.standard_error < standard.standard_error);
}

TEST_CASE("engine rejects invalid configuration",
          "[monte-carlo][validation]") {
    const mc::EuropeanCall call{kOption.strike};
    const mc::MonteCarloEngine engine;

    auto config = simulation_config();
    config.num_paths = 0;
    REQUIRE_THROWS_AS(engine.price(call, kMarket, kOption, config), std::invalid_argument);

    config = simulation_config();
    config.num_threads = 0;
    REQUIRE_THROWS_AS(engine.price(call, kMarket, kOption, config), std::invalid_argument);

    config = simulation_config();
    config.antithetic = true;
    config.num_paths = 3;
    REQUIRE_THROWS_AS(engine.price(call, kMarket, kOption, config), std::invalid_argument);

    auto invalid_market = kMarket;
    invalid_market.volatility = -0.1;
    REQUIRE_THROWS_AS(engine.price(call, invalid_market, kOption, simulation_config()),
                      std::invalid_argument);

    invalid_market = kMarket;
    invalid_market.spot = std::numeric_limits<double>::infinity();
    REQUIRE_THROWS_AS(engine.price(call, invalid_market, kOption, simulation_config()),
                      std::invalid_argument);
}
