#pragma once

#include <span>
#include <string>
#include <string_view>

#include "mc/forecasting/walk_forward_backtest.hpp"

namespace mc::cli {

struct BacktestOptions {
    std::string csv_path;
    std::string price_column{"Adj Close"};
    forecasting::BacktestConfig config{};
};

[[nodiscard]] BacktestOptions parse_backtest_options(
    std::span<const std::string_view> arguments);

}
