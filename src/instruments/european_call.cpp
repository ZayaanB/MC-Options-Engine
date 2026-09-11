#include "mc/instruments/european_call.hpp"

#include <algorithm>

namespace mc {

EuropeanCall::EuropeanCall(const double strike) noexcept : strike_{strike} {}

double EuropeanCall::payoff(const double terminal_price) const noexcept {
    return std::max(terminal_price - strike_, 0.0);
}

double EuropeanCall::strike() const noexcept { return strike_; }

}  // namespace mc
