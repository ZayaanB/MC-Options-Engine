#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mc/forecasting/price_history_csv.hpp"

TEST_CASE("price history reads standard market data CSV", "[forecasting][csv]") {
    std::istringstream input{
        "Date,Open,High,Low,Close,Adj Close,Volume\r\n"
        "2024-01-02,100,102,99,101,100.5,1000\r\n"
        "2024-01-03,101,104,100,103,102.5,1200\r\n"
        "\r\n"
        "2024-01-04,103,105,102,104,103.5,900\r\n"};

    const auto history = mc::forecasting::read_price_history_csv(input);

    REQUIRE(history.dates ==
            std::vector<std::string>{"2024-01-02", "2024-01-03", "2024-01-04"});
    REQUIRE(history.adjusted_closes == std::vector<double>{100.5, 102.5, 103.5});
}

TEST_CASE("price history supports a selected close column", "[forecasting][csv]") {
    std::istringstream input{
        "Date,Close,Adj Close\n"
        "2024-01-02,101,100.5\n"
        "2024-01-03,103,102.5\n"};

    const auto history = mc::forecasting::read_price_history_csv(input, "Close");

    REQUIRE(history.adjusted_closes == std::vector<double>{101.0, 103.0});
}

TEST_CASE("price history handles quoted fields", "[forecasting][csv]") {
    std::istringstream input{
        "\"Date\",\"Adjusted, Close\"\n"
        "\"2024-01-02\",\"100.5\"\n"};

    const auto history =
        mc::forecasting::read_price_history_csv(input, "Adjusted, Close");

    REQUIRE(history.dates.front() == "2024-01-02");
    REQUIRE(history.adjusted_closes.front() == 100.5);
}

TEST_CASE("price history rejects malformed files", "[forecasting][csv][validation]") {
    std::istringstream empty;
    std::istringstream missing_column{"Date,Close\n2024-01-02,100\n"};
    std::istringstream invalid_price{
        "Date,Adj Close\n2024-01-02,not-available\n"};
    std::istringstream missing_field{
        "Date,Adj Close,Volume\n2024-01-02,100\n"};
    std::istringstream unterminated{
        "Date,Adj Close\n2024-01-02,\"100\n"};
    std::istringstream reversed{
        "Date,Adj Close\n2024-01-03,100\n2024-01-02,101\n"};
    std::istringstream bad_format{
        "Date,Adj Close\n01/02/2024,100\n"};
    std::istringstream bad_calendar_date{
        "Date,Adj Close\n2024-02-30,100\n"};

    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(empty),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(missing_column),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(invalid_price),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(missing_field),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(unterminated),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(reversed),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(bad_format),
                      std::invalid_argument);
    REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(bad_calendar_date),
                      std::invalid_argument);
}
