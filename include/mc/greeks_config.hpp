#pragma once

#include <optional>

namespace mc {

struct GreeksConfig {
    std::optional<double> spot_bump{};
    double volatility_bump{0.01};
};

}
