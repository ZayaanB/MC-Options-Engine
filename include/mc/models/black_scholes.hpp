#pragma once

#include "mc/market_data.hpp"

namespace mc {

class BlackScholesModel {
public:
    BlackScholesModel(const MarketData& market, double maturity) noexcept;

    [[nodiscard]] double terminal_price(double standard_normal) const noexcept;
    [[nodiscard]] double discount_factor() const noexcept;

private:
    double spot_;
    double drift_;
    double diffusion_;
    double discount_factor_;
};

}  // namespace mc
