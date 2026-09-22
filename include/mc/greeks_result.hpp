#pragma once

namespace mc {

struct GreeksResult {
    double delta{};
    double gamma{};
    // Price change per one volatility percentage point.
    double vega{};
};

}  // namespace mc
