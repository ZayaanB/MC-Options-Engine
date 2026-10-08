#pragma once

#include <cstddef>
#include <cmath>
#include <stdexcept>

#include "mc/market_data.hpp"

namespace mc {

class GeometricBrownianMotion {
public:
    GeometricBrownianMotion(const MarketData& market, double maturity,
                            std::size_t num_steps);

    [[nodiscard]] double initial_price() const noexcept { return initial_price_; }
    [[nodiscard]] double advance(const double current_price,
                                 const double standard_normal) const {
        const double price = current_price * std::exp(step_drift_ + step_diffusion_ * standard_normal);
        if (!std::isfinite(price)) {
            throw std::overflow_error{"simulated path price exceeds the finite numeric range"};
        }
        return price;
    }
    [[nodiscard]] double time_step() const noexcept { return time_step_; }
    [[nodiscard]] std::size_t num_steps() const noexcept { return num_steps_; }
    [[nodiscard]] double discount_factor() const noexcept { return discount_factor_; }

private:
    double initial_price_{};
    double step_drift_{};
    double step_diffusion_{};
    double time_step_{};
    std::size_t num_steps_{};
    double discount_factor_{};
};

}
