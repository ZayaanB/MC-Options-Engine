#include "mc/statistics/running_statistics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace mc {
namespace {

constexpr double undefined_statistic() noexcept {
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace

void RunningStatistics::add(const double value) {
    if (count_ == std::numeric_limits<std::uint64_t>::max()) {
        throw std::overflow_error{"running statistics sample count overflow"};
    }

    ++count_;
    const double delta = value - mean_;
    mean_ += delta / static_cast<double>(count_);
    const double delta_from_new_mean = value - mean_;
    m2_ += delta * delta_from_new_mean;
}

void RunningStatistics::merge(const RunningStatistics& other) {
    if (other.empty()) {
        return;
    }
    if (empty()) {
        *this = other;
        return;
    }
    if (other.count_ > std::numeric_limits<std::uint64_t>::max() - count_) {
        throw std::overflow_error{"running statistics sample count overflow"};
    }

    const std::uint64_t combined_count = count_ + other.count_;
    const double count = static_cast<double>(count_);
    const double other_count = static_cast<double>(other.count_);
    const double total_count = static_cast<double>(combined_count);
    const double delta = other.mean_ - mean_;

    mean_ += delta * (other_count / total_count);
    m2_ += other.m2_ + delta * delta * (count * other_count / total_count);
    count_ = combined_count;
}

std::uint64_t RunningStatistics::count() const noexcept { return count_; }

bool RunningStatistics::empty() const noexcept { return count_ == 0; }

double RunningStatistics::mean() const noexcept {
    return empty() ? undefined_statistic() : mean_;
}

double RunningStatistics::variance() const noexcept {
    if (count_ < 2) {
        return undefined_statistic();
    }

    return std::max(m2_, 0.0) / static_cast<double>(count_ - 1);
}

double RunningStatistics::standard_error() const noexcept {
    if (count_ < 2) {
        return undefined_statistic();
    }

    return std::sqrt(variance() / static_cast<double>(count_));
}

}  // namespace mc
