cmake_minimum_required(VERSION 3.20)

project(
    mc_options_engine
    VERSION 0.1.0
    DESCRIPTION "C++20 Monte Carlo derivatives pricing and risk engine"
    LANGUAGES CXX
)

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

function(mc_enable_warnings target_name)
    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(${target_name} PRIVATE /W4 /permissive-)
    endif()
endfunction()

add_library(
    mcpricing
    src/instruments/european_call.cpp
    src/instruments/european_put.cpp
    src/pricing/analytical_black_scholes.cpp
    src/statistics/running_statistics.cpp
)
target_compile_features(mcpricing PUBLIC cxx_std_20)
target_include_directories(
    mcpricing
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)
mc_enable_warnings(mcpricing)

add_executable(mcprice src/main.cpp)
target_compile_features(mcprice PRIVATE cxx_std_20)
target_link_libraries(mcprice PRIVATE mcpricing)
mc_enable_warnings(mcprice)

include(CTest)

if(BUILD_TESTING)
    find_package(Catch2 3 QUIET)

    if(NOT Catch2_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            Catch2
            URL https://github.com/catchorg/Catch2/archive/refs/tags/v3.15.3.tar.gz
            DOWNLOAD_EXTRACT_TIMESTAMP FALSE
        )
        FetchContent_MakeAvailable(Catch2)
    endif()

    add_executable(
        mc_tests
        tests/test_black_scholes.cpp
        tests/test_domain_types.cpp
        tests/test_payoffs.cpp
        tests/test_statistics.cpp
        tests/test_smoke.cpp
    )
    target_compile_features(mc_tests PRIVATE cxx_std_20)
    target_link_libraries(mc_tests PRIVATE mcpricing Catch2::Catch2WithMain)
    mc_enable_warnings(mc_tests)

    add_test(NAME mc_tests COMMAND mc_tests)
endif()
