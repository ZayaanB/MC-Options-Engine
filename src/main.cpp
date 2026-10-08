#include <algorithm>
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

#include "mc/cli/backtest_options.hpp"
#include "mc/cli/drift_options.hpp"
#include "mc/cli/forecast_options.hpp"
#include "mc/cli/price_options.hpp"
#include "mc/cli/volatility_options.hpp"
#include "mc/forecasting/historical_gbm.hpp"
#include "mc/forecasting/price_history_csv.hpp"
#include "mc/forecasting/walk_forward_backtest.hpp"
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
  mcprice backtest --csv FILE [options]

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
  --metadata FILE               Optional daily-history provenance key=value file
  --lookback-days N             Fit the latest N returns (default: full history)
  --horizon-days N              Forecast horizon in trading days (default: 20)
  --trading-days N              Trading days per year (default: 252)
  --volatility-model sample|ewma Volatility estimator (default: sample)
  --ewma-decay VALUE            EWMA decay in (0,1) (default: 0.94)
  --drift-model MODEL           historical|zero|shrinkage (default: historical)
  --drift-shrinkage VALUE       Fraction of drift removed in [0,1] (default: 0.5)

Backtest options:
  --csv FILE                    Historical CSV file with Date and price columns
  --price-column NAME           Price column (default: Adj Close)
  --metadata FILE               Optional daily-history provenance key=value file
  --format text|csv             Summary or full-precision forecast rows (default: text)
  --lookback-days N             Prior returns per model fit (default: 252)
  --horizon-days N              Forecast horizon in trading days (default: 20)
  --step-days N                 Days between forecast origins (default: 1)
  --trading-days N              Trading days per year (default: 252)
  --volatility-model sample|ewma Volatility estimator (default: sample)
  --ewma-decay VALUE            EWMA decay in (0,1) (default: 0.94)
  --drift-model MODEL           historical|zero|shrinkage (default: historical)
  --drift-shrinkage VALUE       Fraction of drift removed in [0,1] (default: 0.5)
  --bootstrap-samples N         Paired bootstrap samples (default: 10000)
  --bootstrap-seed N            Unsigned bootstrap seed (default: 42)

General:
  --help                        Show this help
)";

std::string_view trading_day_word(const std::size_t days) noexcept {
    return days == 1 ? "day" : "days";
}

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

std::optional<mc::forecasting::PriceHistoryMetadata> load_metadata(
    const std::string& path, const std::string& price_column,
    const std::string& final_date) {
    if (path.empty()) {
        return std::nullopt;
    }
    auto metadata = mc::forecasting::load_price_history_metadata(path);
    if (metadata.price_column != price_column) {
        throw std::invalid_argument{"metadata price_column does not match --price-column"};
    }
    if (metadata.retrieved_on < final_date) {
        throw std::invalid_argument{"metadata retrieval date precedes the final history date"};
    }
    return metadata;
}

void print_history_context(
    const std::optional<mc::forecasting::PriceHistoryMetadata>& metadata,
    const mc::forecasting::HistoryDiagnostics& diagnostics) {
    if (metadata) {
        std::cout << "Provider (declared):     " << metadata->provider << '\n'
                  << "Symbol (declared):       " << metadata->symbol << '\n'
                  << "Adjustment (declared):   " << metadata->adjustment << '\n'
                  << "Retrieved on:            " << metadata->retrieved_on << '\n'
                  << "Source URL:              " << metadata->source_url << '\n'
                  << "Frequency (declared):    " << metadata->frequency << '\n';
    } else {
        std::cout << "Provenance:              unspecified; adjustment unverified\n";
    }
    std::cout << "Largest calendar gap:    " << diagnostics.maximum_gap_days << " days\n";
    if (diagnostics.gaps_over_four_days > 0 || diagnostics.weekend_rows > 0) {
        std::cout << "History warning:         " << diagnostics.gaps_over_four_days
                  << " gaps over 4 calendar days; " << diagnostics.weekend_rows
                  << " weekend rows. Check daily cadence and missing sessions.\n";
    }
    std::cout << "Calendar check:          heuristic only; no exchange holiday calendar\n";
}

void run_forecast(const mc::cli::ForecastOptions& options) {
    const auto history =
        mc::forecasting::load_price_history_csv(options.csv_path, options.price_column);
    const auto metadata = load_metadata(options.metadata_path, options.price_column,
                                        history.dates.back());
    const auto training = mc::forecasting::forecast_training_prices(
        history.adjusted_closes, options.lookback_days);
    const auto training_dates = std::span<const std::string>{history.dates}.last(training.size());
    const auto diagnostics = mc::forecasting::diagnose_history(training_dates);
    const auto model = mc::forecasting::estimate_gbm(
        training, options.volatility_estimator,
        options.ewma_decay, options.trading_days_per_year,
        options.drift_estimator, options.drift_shrinkage);
    const auto forecast = mc::forecasting::forecast_price(
        model, history.adjusted_closes.back(), options.horizon_days);

    std::cout << std::fixed << std::setprecision(6)
              << "Historical GBM Forecast\n"
              << "=======================\n\n"
              << "Scenario only; not an option value or trading signal.\n\n"
              << "CSV:                     " << options.csv_path << '\n'
              << "Price column:            " << options.price_column << '\n'
              << "History:                 " << training_dates.front() << " to "
              << history.dates.back() << '\n'
              << "Price observations:      " << training.size() << '\n'
              << "Return observations:     " << model.return_observations << '\n'
              << "Volatility model:        "
              << mc::cli::volatility_estimator_name(options.volatility_estimator)
              << '\n';
    print_history_context(metadata, diagnostics);
    std::cout << "Lookback:                ";
    if (options.lookback_days) {
        std::cout << *options.lookback_days << " trading days\n";
    } else {
        std::cout << "full history\n";
    }
    if (options.volatility_estimator ==
        mc::forecasting::VolatilityEstimator::ewma) {
        std::cout << "EWMA decay:              " << options.ewma_decay << '\n';
    }
    std::cout << "Drift model:             "
              << mc::cli::drift_estimator_name(options.drift_estimator) << '\n';
    if (options.drift_estimator ==
        mc::forecasting::DriftEstimator::shrinkage) {
        std::cout << "Drift shrinkage:         " << options.drift_shrinkage << '\n';
    }
    std::cout
              << "Current selected price:  " << forecast.current_price << '\n'
              << "Horizon:                 " << forecast.horizon_days
              << " trading " << trading_day_word(forecast.horizon_days) << '\n'
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

void print_error_metrics(const std::string_view label,
                         const mc::forecasting::ForecastErrorMetrics& metrics) {
    std::cout << label << '\n'
              << "  MAE:                   " << metrics.mean_absolute_error << '\n'
              << "  RMSE:                  " << metrics.root_mean_squared_error << '\n'
              << "  MAPE:                  "
              << metrics.mean_absolute_percentage_error * 100.0 << "%\n"
              << "  Directional accuracy:  ";
    if (metrics.directional_predictions == 0) {
        std::cout << "unavailable (no directional forecasts)\n";
    } else {
        std::cout << metrics.directional_accuracy * 100.0 << "% ("
                  << metrics.directionally_correct << "/"
                  << metrics.directional_predictions << ", 95% Wilson ["
                  << metrics.directional_lower_95 * 100.0 << "%, "
                  << metrics.directional_upper_95 * 100.0 << "%]; assumes independent trials)\n";
    }
}

void print_interval_metrics(
    const std::string_view label,
    const mc::forecasting::IntervalMetrics& metrics) {
    std::cout << label << " interval\n"
              << "  Coverage:              " << metrics.coverage * 100.0 << "%\n"
              << "  Mean width:            " << metrics.mean_width << '\n'
              << "  Mean interval score:   " << metrics.mean_interval_score << '\n';
}

std::string_view conclusion_name(
    const mc::forecasting::ComparisonConclusion conclusion) noexcept {
    switch (conclusion) {
        case mc::forecasting::ComparisonConclusion::better:
            return "better";
        case mc::forecasting::ComparisonConclusion::worse:
            return "worse";
        case mc::forecasting::ComparisonConclusion::inconclusive:
            return "inconclusive";
    }
    return "unknown";
}

void run_backtest(const mc::cli::BacktestOptions& options) {
    const auto history =
        mc::forecasting::load_price_history_csv(options.csv_path, options.price_column);
    const auto metadata = load_metadata(options.metadata_path, options.price_column,
                                        history.dates.back());
    const auto result =
        mc::forecasting::walk_forward_backtest(history.adjusted_closes, options.config);
    if (options.csv_output) {
        std::cout << "origin_date,target_date,current_price,forecast_price,actual_price,"
                     "lower_80,upper_80,lower_90,upper_90,lower_95,upper_95,"
                     "probability_above_current,latest_price_forecast,historical_drift_forecast,"
                     "zero_drift_forecast,momentum_forecast,mean_reversion_forecast\n"
                  << std::setprecision(17);
        for (const auto& point : result.points) {
            std::cout << history.dates[point.origin_index] << ','
                      << history.dates[point.target_index] << ',' << point.current_price << ','
                      << point.forecast_price << ',' << point.actual_price << ','
                      << point.lower_80 << ',' << point.upper_80 << ','
                      << point.lower_90 << ',' << point.upper_90 << ','
                      << point.lower_95 << ',' << point.upper_95 << ','
                      << point.probability_above_current << ','
                      << point.latest_price_forecast << ',' << point.historical_drift_forecast << ','
                      << point.zero_drift_forecast << ',' << point.momentum_forecast << ','
                      << point.mean_reversion_forecast << '\n';
        }
        return;
    }
    const auto diagnostics = mc::forecasting::diagnose_history(history.dates);
    const auto& first = result.points.front();
    const auto& last = result.points.back();

    std::cout << std::fixed << std::setprecision(6)
              << "Historical GBM Walk-Forward Backtest\n"
              << "====================================\n\n"
              << "Each forecast uses only data available at its origin.\n\n"
              << "CSV:                     " << options.csv_path << '\n'
              << "Price column:            " << options.price_column << '\n'
              << "Evaluation period:       " << history.dates[first.origin_index]
              << " to " << history.dates[last.target_index] << '\n'
              << "Lookback:                " << options.config.lookback_days
              << " trading " << trading_day_word(options.config.lookback_days) << '\n'
              << "Horizon:                 " << options.config.horizon_days
              << " trading " << trading_day_word(options.config.horizon_days) << '\n'
              << "Step:                    " << options.config.step_days
              << " trading " << trading_day_word(options.config.step_days) << '\n'
              << "Forecasts:               " << result.points.size() << '\n'
              << "Volatility model:        "
              << mc::cli::volatility_estimator_name(
                     options.config.volatility_estimator)
              << '\n';
    print_history_context(metadata, diagnostics);
    if (options.config.volatility_estimator ==
        mc::forecasting::VolatilityEstimator::ewma) {
        std::cout << "EWMA decay:              " << options.config.ewma_decay << '\n';
    }
    std::cout << "Drift model:             "
              << mc::cli::drift_estimator_name(
                     options.config.drift_estimator)
              << '\n';
    if (options.config.drift_estimator ==
        mc::forecasting::DriftEstimator::shrinkage) {
        std::cout << "Drift shrinkage:         "
                  << options.config.drift_shrinkage << '\n';
    }
    std::cout << "Benchmark window:        "
              << std::min<std::size_t>(20, options.config.lookback_days)
              << " trading days\n"
              << "Bootstrap samples:       "
              << options.config.bootstrap_samples << '\n'
              << "Bootstrap seed:          " << options.config.bootstrap_seed << '\n'
              << "Overlapping targets:     "
              << (options.config.step_days < options.config.horizon_days ? "yes" : "no")
              << "\n\n";
    print_error_metrics("Selected GBM", result.selected_model);
    print_error_metrics("Latest-price baseline", result.latest_price);
    print_error_metrics("Historical-drift GBM baseline",
                        result.historical_drift);
    print_error_metrics("Zero-drift GBM baseline", result.zero_drift);
    print_error_metrics("Momentum baseline", result.momentum);
    print_error_metrics("Mean-reversion baseline", result.mean_reversion);
    std::cout << "\nPaired MAE comparison with latest-price baseline\n"
              << "  Absolute improvement:  "
              << result.mae_improvement.absolute_improvement << '\n';
    if (std::isfinite(result.mae_improvement.relative_improvement)) {
        std::cout << "  Relative improvement:  "
                  << result.mae_improvement.relative_improvement * 100.0
                  << "%\n";
    } else {
        std::cout << "  Relative improvement:  unavailable (zero baseline error)\n";
    }
    if (std::isfinite(result.mae_improvement.lower_95)) {
        std::cout << "  95% bootstrap interval:["
              << result.mae_improvement.lower_95 << ", "
              << result.mae_improvement.upper_95 << "]\n";
    } else {
        std::cout << "  95% bootstrap interval:unavailable (fewer than 10 effective blocks)\n";
    }
    std::cout << "  Block length:          "
              << result.mae_improvement.block_length << '\n'
              << "  Conclusion:            "
              << conclusion_name(result.mae_improvement.conclusion) << "\n\n";
    print_interval_metrics("80%", result.interval_80);
    print_interval_metrics("90%", result.interval_90);
    print_interval_metrics("95%", result.interval_95);
    std::cout << "Probability above current\n"
              << "  Brier score:           "
              << result.probability.brier_score << '\n'
              << "  Calibration:\n";
    for (const auto& bucket : result.probability.calibration) {
        if (bucket.observations == 0) {
            continue;
        }
        std::cout << "    [" << bucket.lower_probability * 100.0 << "%, "
                  << bucket.upper_probability * 100.0 << "%]: n="
                  << bucket.observations << ", mean forecast="
                  << bucket.mean_forecast_probability * 100.0
                  << "%, observed above=" << bucket.observed_frequency * 100.0
                  << "%\n";
    }
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
        if (command != "price" && command != "forecast" && command != "backtest") {
            throw std::invalid_argument{"unknown command: " + std::string{arguments.front()}};
        }
        arguments.erase(arguments.begin());
        if (!arguments.empty() && (arguments.front() == "--help" || arguments.front() == "-h")) {
            std::cout << kUsage;
            return 0;
        }

        if (command == "price") {
            run_price(mc::cli::parse_price_options(arguments));
        } else if (command == "forecast") {
            run_forecast(mc::cli::parse_forecast_options(arguments));
        } else {
            run_backtest(mc::cli::parse_backtest_options(arguments));
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n\n" << kUsage;
        return 1;
    }
}
