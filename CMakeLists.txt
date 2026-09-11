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

add_executable(mcprice src/main.cpp)
target_compile_features(mcprice PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(mcprice PRIVATE -Wall -Wextra -Wpedantic)
elseif(MSVC)
    target_compile_options(mcprice PRIVATE /W4 /permissive-)
endif()

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

    add_executable(mc_tests tests/test_smoke.cpp)
    target_compile_features(mc_tests PRIVATE cxx_std_20)
    target_link_libraries(mc_tests PRIVATE Catch2::Catch2WithMain)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(mc_tests PRIVATE -Wall -Wextra -Wpedantic)
    elseif(MSVC)
        target_compile_options(mc_tests PRIVATE /W4 /permissive-)
    endif()

    add_test(NAME mc_tests COMMAND mc_tests)
endif()
