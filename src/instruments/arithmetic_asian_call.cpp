#include "mc/instruments/arithmetic_asian_call.hpp"

#include <algorithm>

namespace mc {

ArithmeticAsianCall::ArithmeticAsianCall(const double strike) noexcept : strike_{strike} {}

double ArithmeticAsianCall::payoff(const double arithmetic_average) const noexcept {
    return std::max(arithmetic_average - strike_, 0.0);
}

double ArithmeticAsianCall::strike() const noexcept { return strike_; }

}
