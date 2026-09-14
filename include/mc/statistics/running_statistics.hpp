#pragma once

#include <cstdint>

namespace mc {

class RunningStatistics {
public:
    void add(double value);
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
