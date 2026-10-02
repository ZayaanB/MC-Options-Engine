#include <cmath>
#include <exception>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "mc/cli/forecast_options.hpp"
#include "mc/cli/price_options.hpp"
#include "mc/forecasting/historical_gbm.hpp"
#include "mc/forecasting/price_history_csv.hpp"
#include "mc/instruments/arithmetic_asian_call.hpp"
#include "mc/instruments/european_call.hpp"
#include "mc/instruments/european_put.hpp"
#include "mc/instruments/instrument.hpp"
#include "mc/pricing/analytical_black_scholes.hpp"
#include "mc/pricing/monte_carlo_engine.hpp"
#include "mc/pricing/path_monte_carlo_engine.hpp"
#include "mc/validation.hpp"

namespace {

constexpr std::string_view kUsage = R"(Usage:
  mcprice price [options]
  mcprice forecast --csv FILE [options]

Price options:
  --type call|put|asian-call    Instrument type (default: call)
  --method mc|analytical|both   Pricing method (default: both)
  --spot VALUE                  Spot price (default: 100)
  --strike VALUE                Strike price (default: 100)
  --rate VALUE                  Continuously compounded rate (default: 0.05)
  --volatility VALUE            Annualized volatility (default: 0.20)
  --maturity VALUE              Maturity in years (default: 1)
  --paths N                     Total Monte Carlo trajectories (default: 1000000)
  --threads N                   Worker threads (default: 1)
  --batch-size N                Observations per worker batch (default: automatic)
  --steps N                     Monitoring steps for path simulation (default: 1)
  --seed N                      Unsigned RNG seed (default: 42)
  --antithetic                  Enable antithetic variates; paths must be even

Forecast options:
  --csv FILE                    Historical CSV file with Date and price columns
  --price-column NAME           Price column (default: Adj Close)
  --horizon-days N              Forecast horizon in trading days (default: 20)
  --trading-days N              Trading days per year (default: 252)

General:
  --help                        Show this help
)";

double analytical_price(const mc::cli::OptionType type, const mc::MarketData& market,
                        const mc::OptionParameters& option) {
    if (type == mc::cli::OptionType::call) {
        return mc::black_scholes_call(market, option);
    }
    if (type == mc::cli::OptionType::put) {
        return mc::black_scholes_put(market, option);
    }
    throw std::logic_error{"arithmetic Asian call has no analytical implementation"};
}

std::unique_ptr<mc::Instrument> make_instrument(const mc::cli::OptionType type,
                                                const double strike) {
    if (type == mc::cli::OptionType::call) {
        return std::make_unique<mc::EuropeanCall>(strike);
    }
    if (type == mc::cli::OptionType::put) {
        return std::make_unique<mc::EuropeanPut>(strike);
    }
    throw std::logic_error{"arithmetic Asian call is not a terminal-only instrument"};
}

void print_inputs(const mc::cli::PriceOptions& options) {
    if (options.type == mc::cli::OptionType::asian_call) {
        std::cout << mc::cli::option_type_name(options.type) << '\n'
                  << "Method:                 mc (analytical reference unavailable)\n";
    } else {
        std::cout << "European " << mc::cli::option_type_name(options.type) << '\n'
                  << "Method:                 "
                  << mc::cli::pricing_method_name(options.method) << '\n';
    }
    std::cout << "Spot:                   " << options.market.spot << '\n'
              << "Strike:                 " << options.option.strike << '\n'
              << "Rate:                   " << options.market.risk_free_rate << '\n'
              << "Volatility:             " << options.market.volatility << '\n'
              << "Maturity:               " << options.option.maturity << " years\n";
}

void print_monte_carlo_result(const mc::PricingResult& result,
                              const mc::cli::PriceOptions& options,
                              const std::optional<double> reference) {
    std::cout << "Monte Carlo estimate:   " << result.price << '\n';
    if (reference) {
        std::cout << "Absolute difference:    " << std::abs(result.price - *reference) << '\n';
    }
    std::cout << "Standard error:         " << result.standard_error << '\n'
              << "95% confidence interval:[" << result.confidence_lower << ", "
              << result.confidence_upper << "]\n"
              << "Paths:                  " << result.paths << '\n'
              << "Observations:           " << result.observations << '\n'
              << "Threads:                " << options.simulation.num_threads << '\n'
              << "Batch size:             ";
    if (options.simulation.batch_size == 0) {
        std::cout << "automatic\n";
    } else {
        std::cout << options.simulation.batch_size << " observations\n";
    }
    std::cout
              << "Steps:                  " << options.simulation.num_steps << '\n'
              << "Seed:                   " << options.simulation.seed << '\n'
              << "Antithetic:             "
              << (options.simulation.antithetic ? "yes" : "no") << '\n'
              << "Runtime:                " << result.runtime_seconds << " s\n"
              << "Throughput:             " << result.paths_per_second / 1'000'000.0
              << " M paths/s\n";
}

void run_price(const mc::cli::PriceOptions& options) {
    mc::validate(options.market);
    mc::validate(options.option);

    const bool has_analytical = options.type != mc::cli::OptionType::asian_call;
    const bool run_analytical =
        has_analytical && options.method != mc::cli::PricingMethod::monte_carlo;
    const bool run_monte_carlo =
        !has_analytical || options.method != mc::cli::PricingMethod::analytical;
    if (run_monte_carlo) {
        mc::validate(options.simulation);
    }
    if (options.type == mc::cli::OptionType::asian_call &&
        options.simulation.num_steps == 0) {
        throw std::invalid_argument{"steps must be positive for path simulation"};
    }
    const std::optional<double> reference =
        run_analytical ? std::optional<double>{
                             analytical_price(options.type, options.market, options.option)}
                       : std::nullopt;

    std::cout << std::fixed << std::setprecision(6)
              << "Monte Carlo Options Pricing Engine\n"
              << "==================================\n\n";
    print_inputs(options);
    std::cout << '\n';

    if (run_analytical) {
        std::cout << "Analytical price:       " << *reference << '\n';
    } else if (!has_analytical) {
        std::cout << "Analytical reference:   unavailable for arithmetic Asian call\n";
    }
    if (run_monte_carlo) {
        mc::PricingResult result;
        if (options.type == mc::cli::OptionType::asian_call) {
            const mc::ArithmeticAsianCall instrument{options.option.strike};
            const mc::PathMonteCarloEngine engine;
            result = engine.price(instrument, options.market, options.option,
                                  options.simulation);
        } else {
            const auto instrument = make_instrument(options.type, options.option.strike);
            const mc::MonteCarloEngine engine;
            result = engine.price(*instrument, options.market, options.option,
                                  options.simulation);
        }
        print_monte_carlo_result(result, options, reference);
    }
}

void run_forecast(const mc::cli::ForecastOptions& options) {
    const auto history =
        mc::forecasting::load_price_history_csv(options.csv_path, options.price_column);
    const auto model = mc::forecasting::estimate_historical_gbm(
        history.adjusted_closes, options.trading_days_per_year);
    const auto forecast = mc::forecasting::forecast_price(
        model, history.adjusted_closes.back(), options.horizon_days);

    std::cout << std::fixed << std::setprecision(6)
              << "Historical GBM Forecast\n"
              << "=======================\n\n"
              << "Scenario only; not an option value or trading signal.\n\n"
              << "CSV:                     " << options.csv_path << '\n'
              << "Price column:            " << options.price_column << '\n'
              << "History:                 " << history.dates.front() << " to "
              << history.dates.back() << '\n'
              << "Price observations:      " << history.adjusted_closes.size() << '\n'
              << "Return observations:     " << model.return_observations << '\n'
              << "Current adjusted close:  " << forecast.current_price << '\n'
              << "Horizon:                 " << forecast.horizon_days
              << " trading days\n"
              << "Annualized drift:        " << model.annualized_drift * 100.0 << "%\n"
              << "Annualized volatility:   " << model.annualized_volatility * 100.0
              << "%\n"
              << "Expected price:          " << forecast.expected_price << '\n'
              << "Median price:            " << forecast.median_price << '\n'
              << "95% model interval:      [" << forecast.lower_95 << ", "
              << forecast.upper_95 << "]\n"
              << "Probability above today: "
              << forecast.probability_above_current * 100.0 << "%\n";
}

}

int main(const int argc, const char* const argv[]) {
    try {
        std::vector<std::string_view> arguments;
        arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
        for (int index = 1; index < argc; ++index) {
            arguments.emplace_back(argv[index]);
        }

        if (arguments.empty() || arguments.front() == "--help" ||
            arguments.front() == "-h") {
            std::cout << kUsage;
            return 0;
        }
        const std::string_view command = arguments.front();
        if (command != "price" && command != "forecast") {
            throw std::invalid_argument{"unknown command: " + std::string{arguments.front()}};
        }
        arguments.erase(arguments.begin());
        if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
            std::cout << kUsage;
            return 0;
        }

        if (command == "price") {
            run_price(mc::cli::parse_price_options(arguments));
        } else {
            run_forecast(mc::cli::parse_forecast_options(arguments));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n" << kUsage;
        return 1;
    }
}
