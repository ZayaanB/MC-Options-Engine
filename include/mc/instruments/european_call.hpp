#pragma once

#include "mc/instruments/instrument.hpp"

namespace mc {

class EuropeanCall final : public Instrument {
public:
    explicit EuropeanCall(double strike) noexcept;

    [[nodiscard]] double payoff(double terminal_price) const noexcept override;
    [[nodiscard]] double strike() const noexcept;

private:
    double strike_;
};

}  // namespace mc
