#pragma once

#include <span>
#include <string_view>

#include "mc/market_data.hpp"
#include "mc/option_parameters.hpp"
#include "mc/simulation_config.hpp"

namespace mc::cli {

enum class OptionType { call, put };
enum class PricingMethod { monte_carlo, analytical, both };

struct PriceOptions {
    OptionType type{OptionType::call};
    PricingMethod method{PricingMethod::both};
    MarketData market{100.0, 0.05, 0.20};
    OptionParameters option{100.0, 1.0};
    SimulationConfig simulation{1'000'000, 42, 1, 0, false};
};

[[nodiscard]] PriceOptions parse_price_options(std::span<const std::string_view> arguments);
[[nodiscard]] std::string_view option_type_name(OptionType type) noexcept;
[[nodiscard]] std::string_view pricing_method_name(PricingMethod method) noexcept;

}  // namespace mc::cli
