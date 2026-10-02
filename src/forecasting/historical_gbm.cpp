#include "mc/forecasting/historical_gbm.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "mc/statistics/running_statistics.hpp"

namespace mc::forecasting {
namespace {

constexpr double kNormal95 = 1.959963984540054;

void require_positive_finite(const double value, const char* const name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument{std::string{name} + " must be finite and positive"};
    }
}

double standard_normal_cdf(const double value) {
    return 0.5 * std::erfc(-value / std::sqrt(2.0));
}

void require_finite_result(const double value) {
    if (!std::isfinite(value)) {
        throw std::overflow_error{"forecast exceeds the finite numeric range"};
    }
}

}

HistoricalGbmModel estimate_historical_gbm(
    const std::span<const double> adjusted_closes,
    const double trading_days_per_year) {
    require_positive_finite(trading_days_per_year, "trading days per year");
    if (adjusted_closes.size() < 3) {
        throw std::invalid_argument{"at least three adjusted closes are required"};
    }

    RunningStatistics returns;
    require_positive_finite(adjusted_closes.front(), "adjusted close");
    for (std::size_t index = 1; index < adjusted_closes.size(); ++index) {
        require_positive_finite(adjusted_closes[index], "adjusted close");
        returns.add(std::log(adjusted_closes[index] / adjusted_closes[index - 1]));
    }

    const double daily_volatility = std::sqrt(returns.variance());
    const double annualized_volatility =
        daily_volatility * std::sqrt(trading_days_per_year);
    const double annualized_drift =
        returns.mean() * trading_days_per_year +
        0.5 * annualized_volatility * annualized_volatility;

    require_finite_result(annualized_drift);
    require_finite_result(annualized_volatility);

    return HistoricalGbmModel{adjusted_closes.size() - 1,
                              returns.mean(),
                              daily_volatility,
                              annualized_drift,
                              annualized_volatility,
                              trading_days_per_year};
}

PriceForecast forecast_price(const HistoricalGbmModel& model,
                             const double current_price,
                             const std::size_t horizon_days) {
    require_positive_finite(current_price, "current price");
    require_positive_finite(model.trading_days_per_year, "trading days per year");
    if (model.return_observations < 2) {
        throw std::invalid_argument{"model requires at least two return observations"};
    }
    if (!std::isfinite(model.mean_daily_log_return) ||
        !std::isfinite(model.daily_volatility) || model.daily_volatility < 0.0 ||
        !std::isfinite(model.annualized_drift) ||
        !std::isfinite(model.annualized_volatility) || model.annualized_volatility < 0.0) {
        throw std::invalid_argument{"model parameters must be finite and valid"};
    }
    if (horizon_days == 0) {
        throw std::invalid_argument{"forecast horizon must be positive"};
    }

    const double horizon = static_cast<double>(horizon_days);
    const double log_mean = model.mean_daily_log_return * horizon;
    const double log_standard_deviation = model.daily_volatility * std::sqrt(horizon);
    const double median = current_price * std::exp(log_mean);
    const double expected =
        current_price * std::exp(log_mean + 0.5 * log_standard_deviation *
                                                log_standard_deviation);
    const double lower =
        current_price * std::exp(log_mean - kNormal95 * log_standard_deviation);
    const double upper =
        current_price * std::exp(log_mean + kNormal95 * log_standard_deviation);
    const double probability_above_current =
        log_standard_deviation == 0.0
            ? (log_mean > 0.0 ? 1.0 : (log_mean < 0.0 ? 0.0 : 0.5))
            : standard_normal_cdf(log_mean / log_standard_deviation);

    require_finite_result(expected);
    require_finite_result(median);
    require_finite_result(lower);
    require_finite_result(upper);

    return PriceForecast{current_price,
                         horizon_days,
                         expected,
                         median,
                         lower,
                         upper,
                         probability_above_current};
}

}
