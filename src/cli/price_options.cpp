#include "mc/cli/price_options.hpp"

#include <charconv>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_set>

namespace mc::cli {
namespace {

double parse_double(const std::string_view text, const std::string_view option) {
    double value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() || !std::isfinite(value)) {
        throw std::invalid_argument{std::string{option} + " requires a finite number"};
    }
    return value;
}

std::uint64_t parse_unsigned(const std::string_view text, const std::string_view option) {
    std::uint64_t value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size()) {
        throw std::invalid_argument{std::string{option} + " requires a nonnegative integer"};
    }
    return value;
}

std::string_view take_value(const std::span<const std::string_view> arguments,
                            std::size_t& index, const std::string_view option) {
    if (++index == arguments.size()) {
        throw std::invalid_argument{std::string{option} + " requires a value"};
    }
    return arguments[index];
}

}  // namespace

PriceOptions parse_price_options(const std::span<const std::string_view> arguments) {
    PriceOptions options;
    std::unordered_set<std::string_view> seen;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        const std::string_view argument = arguments[index];
        if (!argument.starts_with("--")) {
            throw std::invalid_argument{"unexpected positional argument: " +
                                        std::string{argument}};
        }
        if (!seen.insert(argument).second) {
            throw std::invalid_argument{"duplicate option: " + std::string{argument}};
        }

        if (argument == "--antithetic") {
            options.simulation.antithetic = true;
        } else if (argument == "--type") {
            const auto value = take_value(arguments, index, argument);
            if (value == "call") {
                options.type = OptionType::call;
            } else if (value == "put") {
                options.type = OptionType::put;
            } else {
                throw std::invalid_argument{"--type must be call or put"};
            }
        } else if (argument == "--method") {
            const auto value = take_value(arguments, index, argument);
            if (value == "mc") {
                options.method = PricingMethod::monte_carlo;
            } else if (value == "analytical") {
                options.method = PricingMethod::analytical;
            } else if (value == "both") {
                options.method = PricingMethod::both;
            } else {
                throw std::invalid_argument{"--method must be mc, analytical, or both"};
            }
        } else if (argument == "--spot") {
            options.market.spot = parse_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--strike") {
            options.option.strike = parse_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--rate") {
            options.market.risk_free_rate =
                parse_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--volatility") {
            options.market.volatility =
                parse_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--maturity") {
            options.option.maturity =
                parse_double(take_value(arguments, index, argument), argument);
        } else if (argument == "--paths") {
            options.simulation.num_paths =
                parse_unsigned(take_value(arguments, index, argument), argument);
        } else if (argument == "--threads") {
            const auto value = parse_unsigned(take_value(arguments, index, argument), argument);
            if (value > std::numeric_limits<std::size_t>::max()) {
                throw std::invalid_argument{"--threads is too large for this platform"};
            }
            options.simulation.num_threads = static_cast<std::size_t>(value);
        } else if (argument == "--seed") {
            options.simulation.seed =
                parse_unsigned(take_value(arguments, index, argument), argument);
        } else {
            throw std::invalid_argument{"unknown option: " + std::string{argument}};
        }
    }
    return options;
}

std::string_view option_type_name(const OptionType type) noexcept {
    return type == OptionType::call ? "call" : "put";
}

std::string_view pricing_method_name(const PricingMethod method) noexcept {
    switch (method) {
        case PricingMethod::monte_carlo:
            return "mc";
        case PricingMethod::analytical:
            return "analytical";
        case PricingMethod::both:
            return "both";
    }
    return "unknown";
}

}  // namespace mc::cli
