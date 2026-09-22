#pragma once

#include <optional>

namespace mc {

struct GreeksConfig {
    // An omitted spot bump defaults to 1% of the current spot.
    std::optional<double> spot_bump{};
    // Absolute volatility bump: 0.01 means one volatility percentage point.
    double volatility_bump{0.01};
};

}  // namespace mc
