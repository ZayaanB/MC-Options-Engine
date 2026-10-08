#pragma once

#include <cmath>
#include <stdexcept>

#include "mc/market_data.hpp"

namespace mc {

class BlackScholesModel {
public:
    BlackScholesModel(const MarketData& market, double maturity);

    [[nodiscard]] double terminal_price(const double standard_normal) const {
        const double price = spot_ * std::exp(drift_ + diffusion_ * standard_normal);
        if (!std::isfinite(price)) {
            throw std::overflow_error{"simulated terminal price exceeds the finite numeric range"};
        }
        return price;
    }
    [[nodiscard]] double discount_factor() const noexcept { return discount_factor_; }

private:
    double spot_{};
    double drift_{};
    double diffusion_{};
    double discount_factor_{};
};

}
