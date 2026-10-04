#include "mc/instruments/european_call.hpp"

#include <algorithm>

#include "mc/validation.hpp"

namespace mc {

EuropeanCall::EuropeanCall(const double strike) : strike_{strike} {
    validate_strike(strike);
}

double EuropeanCall::payoff(const double terminal_price) const noexcept {
    return std::max(terminal_price - strike_, 0.0);
}

double EuropeanCall::strike() const noexcept { return strike_; }

}
