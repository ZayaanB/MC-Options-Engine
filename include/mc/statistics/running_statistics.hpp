#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace mc {

class RunningStatistics {
public:
    void add(const double value) {
        if (count_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error{"running statistics sample count overflow"};
        }

        ++count_;
        const double delta = value - mean_;
        mean_ += delta / static_cast<double>(count_);
        const double delta_from_new_mean = value - mean_;
        m2_ += delta * delta_from_new_mean;
    }
    void merge(const RunningStatistics& other);

    [[nodiscard]] std::uint64_t count() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] double mean() const noexcept;
    [[nodiscard]] double variance() const noexcept;
    [[nodiscard]] double standard_error() const noexcept;

private:
    std::uint64_t count_{};
    double mean_{};
    double m2_{};
};

}  // namespace mc
