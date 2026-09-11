#pragma once

#include <cstddef>
#include <cstdint>

namespace mc {

struct SimulationConfig {
    std::uint64_t num_paths{};
    std::uint64_t seed{};
    std::size_t num_threads{};
    std::size_t batch_size{};
    bool antithetic{};
};

}  // namespace mc
