#include "mc/instruments/european_put.hpp"

#include <algorithm>

namespace mc {

EuropeanPut::EuropeanPut(const double strike) noexcept : strike_{strike} {}

double EuropeanPut::payoff(const double terminal_price) const noexcept {
    return std::max(strike_ - terminal_price, 0.0);
}

double EuropeanPut::strike() const noexcept { return strike_; }

}  // namespace mc
