#include "mc/instruments/european_put.hpp"

#include <algorithm>

#include "mc/validation.hpp"

namespace mc {

EuropeanPut::EuropeanPut(const double strike) : strike_{strike} {
    validate_strike(strike);
}

double EuropeanPut::payoff(const double terminal_price) const noexcept {
    return std::max(strike_ - terminal_price, 0.0);
}

double EuropeanPut::strike() const noexcept { return strike_; }

}
