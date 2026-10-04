#include "mc/instruments/arithmetic_asian_call.hpp"

#include <algorithm>

#include "mc/validation.hpp"

namespace mc {

ArithmeticAsianCall::ArithmeticAsianCall(const double strike) : strike_{strike} {
    validate_strike(strike);
}

double ArithmeticAsianCall::payoff(const double arithmetic_average) const noexcept {
    return std::max(arithmetic_average - strike_, 0.0);
}

double ArithmeticAsianCall::strike() const noexcept { return strike_; }

}
