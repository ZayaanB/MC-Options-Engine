#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace mc {

class RunningStatistics {
public:
    void add(const double value) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument{"running statistics values must be finite"};
        }
        if (count_ == std::numeric_limits<std::uint64_t>::max()) {
            throw std::overflow_error{"running statistics sample count overflow"};
        }

        const auto next_count = count_ + 1;
        const double delta = value - mean_;
        const double next_mean = mean_ + delta / static_cast<double>(next_count);
        const double next_m2 = m2_ + delta * (value - next_mean);
        if (!std::isfinite(next_mean) || !std::isfinite(next_m2)) {
            throw std::overflow_error{"running statistics numeric overflow"};
        }
        count_ = next_count;
        mean_ = next_mean;
        m2_ = next_m2;
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

}
