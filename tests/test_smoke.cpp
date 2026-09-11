#include <array>
#include <span>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("test target is compiled as C++20", "[smoke]") {
    const std::array values{1, 2, 3};
    const std::span view{values};

    STATIC_REQUIRE(decltype(view)::extent == 3);
    REQUIRE(view.size() == 3);
    REQUIRE(view.front() == 1);
    REQUIRE(view.back() == 3);
}
