#pragma once

#include <istream>
#include <string>
#include <string_view>
#include <vector>

namespace mc::forecasting {

struct PriceHistory {
    std::vector<std::string> dates;
    std::vector<double> adjusted_closes;
};

[[nodiscard]] PriceHistory read_price_history_csv(
    std::istream& input,
    std::string_view price_column = "Adj Close");

[[nodiscard]] PriceHistory load_price_history_csv(
    const std::string& path,
    std::string_view price_column = "Adj Close");

}
