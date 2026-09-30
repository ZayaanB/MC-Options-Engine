#pragma once

namespace mc {

class ArithmeticAsianCall {
public:
    explicit ArithmeticAsianCall(double strike) noexcept;

    [[nodiscard]] double payoff(double arithmetic_average) const noexcept;
    [[nodiscard]] double strike() const noexcept;

private:
    double strike_{};
};

}
