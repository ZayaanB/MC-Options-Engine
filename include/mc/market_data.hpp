#pragma once

namespace mc {

struct MarketData {
    double spot{};
    double risk_free_rate{};
    double volatility{};
};

}  // namespace mc
