#pragma once

#include <cstddef>

#include "mc/market_data.hpp"

namespace mc {

// Stateless, constant-memory evolution for equally spaced risk-neutral GBM steps.
// Callers retain only the current price and any streaming path statistic they need.
class GeometricBrownianMotion {
public:
    GeometricBrownianMotion(const MarketData& market, double maturity,
                            std::size_t num_steps);

    [[nodiscard]] double initial_price() const noexcept;
    [[nodiscard]] double advance(double current_price,
                                 double standard_normal) const noexcept;
    [[nodiscard]] double time_step() const noexcept;
    [[nodiscard]] std::size_t num_steps() const noexcept;
    [[nodiscard]] double discount_factor() const noexcept;

private:
    double initial_price_{};
    double step_drift_{};
    double step_diffusion_{};
    double time_step_{};
    std::size_t num_steps_{};
    double discount_factor_{};
};

}  // namespace mc
