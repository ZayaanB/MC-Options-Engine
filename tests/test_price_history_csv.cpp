#include <sstream>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

#include "mc/forecasting/price_history_csv.hpp"

TEST_CASE("history diagnostics flag suspicious cadence without rewriting dates", "[forecasting][csv]") {
    const std::array<std::string, 4> daily{"2024-01-12", "2024-01-16", "2024-01-17", "2024-01-18"};
    const auto ordinary = mc::forecasting::diagnose_history(daily);
    REQUIRE(ordinary.weekend_rows == 0);
    REQUIRE(ordinary.gaps_over_four_days == 0);
    REQUIRE(ordinary.maximum_gap_days == 4);
    const std::array<std::string, 3> suspicious{"2024-01-05", "2024-01-06", "2024-02-06"};
    const auto diagnostics = mc::forecasting::diagnose_history(suspicious);
    REQUIRE(diagnostics.weekend_rows == 1);
    REQUIRE(diagnostics.gaps_over_four_days == 1);
    REQUIRE(diagnostics.maximum_gap_days == 31);
    REQUIRE(mc::forecasting::diagnose_history({}).maximum_gap_days == 0);
    const std::array<std::string, 2> duplicate{"2024-01-05", "2024-01-05"};
    REQUIRE_THROWS_AS(mc::forecasting::diagnose_history(duplicate), std::invalid_argument);
    const std::array<std::string, 1> invalid{"2024-02-30"};
    REQUIRE_THROWS_AS(mc::forecasting::diagnose_history(invalid), std::invalid_argument);
}

TEST_CASE("forecast lookback counts returns rather than prices", "[forecasting][csv]") {
    const std::array prices{1.0, 2.0, 3.0, 4.0, 5.0};
    const auto training = mc::forecasting::forecast_training_prices(prices, 2);
    REQUIRE(training.size() == 3);
    REQUIRE(training.front() == 3.0);
    REQUIRE(training.back() == 5.0);
    REQUIRE(training.data() == prices.data() + 2);
    REQUIRE(mc::forecasting::forecast_training_prices(prices, std::nullopt).size() == 5);
    REQUIRE(mc::forecasting::forecast_training_prices(prices, 4).size() == 5);
    for (const std::size_t invalid : {std::size_t{0}, std::size_t{1}, std::size_t{5},
                                    std::numeric_limits<std::size_t>::max()}) {
        REQUIRE_THROWS_AS(mc::forecasting::forecast_training_prices(prices, invalid),
                          std::invalid_argument);
    }
    REQUIRE_THROWS_AS(mc::forecasting::forecast_training_prices({}, std::nullopt),
                      std::invalid_argument);
}

TEST_CASE("daily history metadata preserves provider adjustment semantics", "[forecasting][csv]") {
    const std::string valid =
        "provider=Example\nsymbol=AAPL\nadjustment=split and dividend adjusted\n"
        "retrieved_on=2026-10-07\nsource_url=https://example.test/prices?a=1\n"
        "price_column=Close\nfrequency=daily\n";
    std::istringstream input{valid};
    const auto metadata = mc::forecasting::read_price_history_metadata(input);
    REQUIRE(metadata.provider == "Example");
    REQUIRE(metadata.symbol == "AAPL");
    REQUIRE(metadata.adjustment == "split and dividend adjusted");
    REQUIRE(metadata.retrieved_on == "2026-10-07");
    REQUIRE(metadata.source_url == "https://example.test/prices?a=1");
    REQUIRE(metadata.price_column == "Close");
    REQUIRE(metadata.frequency == "daily");
    for (const auto& text : {valid + "symbol=MSFT\n", valid + "extra=value\n",
                             valid + "bad line\n", std::string{"provider=Example\n"},
                             std::string{}}) {
        std::istringstream bad{text};
        REQUIRE_THROWS_AS(mc::forecasting::read_price_history_metadata(bad),
                          std::invalid_argument);
    }
    for (const auto& replacement : {std::pair{"daily", "monthly"},
                                    std::pair{"2026-10-07", "2026-02-30"},
                                    std::pair{"provider=Example", "provider= "}}) {
        auto text = valid;
        const auto position = text.find(replacement.first);
        text.replace(position, std::string{replacement.first}.size(), replacement.second);
        std::istringstream bad{text};
        REQUIRE_THROWS_AS(mc::forecasting::read_price_history_metadata(bad), std::invalid_argument);
    }
}

TEST_CASE("CSV rejects ambiguous headers and malformed quotes", "[forecasting][csv]") {
    for (const auto* text : {
             "Date,Adj Close,Adj Close\n2024-01-02,100,101\n",
             "Date,Adj Close\n2024-01-02,\"100\"x\n",
             "Date,Adj Close\n2024-01-02,10\"0\n"}) {
        std::istringstream input{text};
        REQUIRE_THROWS_AS(mc::forecasting::read_price_history_csv(input),
                          std::invalid_argument);
    }
}

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
