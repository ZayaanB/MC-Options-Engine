#include <array>
#include <cmath>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include "mc/statistics/running_statistics.hpp"

namespace {

void add_values(mc::RunningStatistics& statistics, const auto& values) {
    for (const double value : values) {
        statistics.add(value);
    }
}

}  // namespace

TEST_CASE("Welford statistics match a hand-calculated sample", "[statistics]") {
    using Catch::Approx;

    mc::RunningStatistics statistics;
    add_values(statistics, std::array{1.0, 2.0, 3.0, 4.0, 5.0});

    REQUIRE(statistics.count() == 5);
    REQUIRE_FALSE(statistics.empty());
    REQUIRE(statistics.mean() == Approx(3.0));
    REQUIRE(statistics.variance() == Approx(2.5));
    REQUIRE(statistics.standard_error() == Approx(std::sqrt(0.5)));
}

TEST_CASE("merging partitions matches a single Welford pass", "[statistics][merge]") {
    using Catch::Approx;

    mc::RunningStatistics first;
    mc::RunningStatistics second;
    mc::RunningStatistics all;
    add_values(first, std::array{1.0, 2.0});
    add_values(second, std::array{3.0, 4.0, 5.0});
    add_values(all, std::array{1.0, 2.0, 3.0, 4.0, 5.0});

    first.merge(second);

    REQUIRE(first.count() == all.count());
    REQUIRE(first.mean() == Approx(all.mean()));
    REQUIRE(first.variance() == Approx(all.variance()));
    REQUIRE(first.standard_error() == Approx(all.standard_error()));
}

TEST_CASE("empty statistics merge without changing populated statistics",
          "[statistics][merge]") {
    using Catch::Approx;

    mc::RunningStatistics empty;
    mc::RunningStatistics populated;
    add_values(populated, std::array{2.0, 4.0, 6.0});

    const auto original_count = populated.count();
    const double original_mean = populated.mean();
    const double original_variance = populated.variance();

    populated.merge(empty);
    REQUIRE(populated.count() == original_count);
    REQUIRE(populated.mean() == Approx(original_mean));
    REQUIRE(populated.variance() == Approx(original_variance));

    empty.merge(populated);
    REQUIRE(empty.count() == original_count);
    REQUIRE(empty.mean() == Approx(original_mean));
    REQUIRE(empty.variance() == Approx(original_variance));
}

TEST_CASE("Welford updates remain stable for values with a large offset", "[statistics]") {
    using Catch::Approx;

    mc::RunningStatistics statistics;
    add_values(statistics,
               std::array{1'000'000'000'001.0, 1'000'000'000'002.0,
                          1'000'000'000'003.0, 1'000'000'000'004.0,
                          1'000'000'000'005.0});

    REQUIRE(statistics.mean() == Approx(1'000'000'000'003.0));
    REQUIRE(statistics.variance() == Approx(2.5));
}

TEST_CASE("undefined sample statistics are reported as NaN", "[statistics][boundary]") {
    mc::RunningStatistics statistics;

    REQUIRE(statistics.empty());
    REQUIRE(statistics.count() == 0);
    REQUIRE(std::isnan(statistics.mean()));
    REQUIRE(std::isnan(statistics.variance()));
    REQUIRE(std::isnan(statistics.standard_error()));

    statistics.add(42.0);
    REQUIRE(statistics.mean() == 42.0);
    REQUIRE(std::isnan(statistics.variance()));
    REQUIRE(std::isnan(statistics.standard_error()));
}

TEST_CASE("constant samples have zero estimated uncertainty", "[statistics]") {
    mc::RunningStatistics statistics;
    add_values(statistics, std::array{7.0, 7.0, 7.0, 7.0});

    REQUIRE(statistics.mean() == 7.0);
    REQUIRE(statistics.variance() == 0.0);
    REQUIRE(statistics.standard_error() == 0.0);
}
