#pragma once

namespace mc {

class Instrument {
public:
    virtual ~Instrument() = default;

    [[nodiscard]] virtual double payoff(double terminal_price) const noexcept = 0;
};

}  // namespace mc
