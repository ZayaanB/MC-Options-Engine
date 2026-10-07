#pragma once

#include <cstddef>
#include <istream>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mc::forecasting {

struct PriceHistory {
    std::vector<std::string> dates;
    std::vector<double> adjusted_closes;
};

struct PriceHistoryMetadata {
    std::string provider;
    std::string symbol;
    std::string adjustment;
    std::string retrieved_on;
    std::string source_url;
    std::string price_column;
    std::string frequency;
};

struct HistoryDiagnostics {
    std::size_t weekend_rows{};
    std::size_t gaps_over_four_days{};
    std::size_t maximum_gap_days{};
};

[[nodiscard]] PriceHistoryMetadata read_price_history_metadata(std::istream& input);
[[nodiscard]] PriceHistoryMetadata load_price_history_metadata(const std::string& path);
[[nodiscard]] HistoryDiagnostics diagnose_history(std::span<const std::string> dates);
[[nodiscard]] std::span<const double> forecast_training_prices(
    std::span<const double> prices, std::optional<std::size_t> lookback_days);

[[nodiscard]] PriceHistory read_price_history_csv(
    std::istream& input,
    std::string_view price_column = "Adj Close");

[[nodiscard]] PriceHistory load_price_history_csv(
    const std::string& path,
    std::string_view price_column = "Adj Close");

}
