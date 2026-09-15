#pragma once

#include <cstdint>

namespace mc {

struct PricingResult {
    double price{};
    double sample_variance{};
    double standard_error{};
    double confidence_lower{};
    double confidence_upper{};
    std::uint64_t paths{};
    double runtime_seconds{};
    double paths_per_second{};
};

}  // namespace mc
