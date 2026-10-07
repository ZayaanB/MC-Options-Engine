#include "mc/forecasting/price_history_csv.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

namespace mc::forecasting {
namespace {

std::vector<std::string> parse_row(const std::string_view row,
                                   const std::size_t line_number) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    bool closed_quote = false;

    for (std::size_t index = 0; index < row.size(); ++index) {
        const char character = row[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1 < row.size() && row[index + 1] == '"') {
                    field.push_back('"');
                    ++index;
                } else {
                    quoted = false;
                    closed_quote = true;
                }
            } else {
                field.push_back(character);
            }
        } else if (character == ',') {
            fields.push_back(field);
            field.clear();
            closed_quote = false;
        } else if (closed_quote || (character == '"' && !field.empty())) {
            throw std::invalid_argument{"malformed quoted field on CSV line " +
                                        std::to_string(line_number)};
        } else if (character == '"' && field.empty()) {
            quoted = true;
        } else {
            field.push_back(character);
        }
    }
    if (quoted) {
        throw std::invalid_argument{"unterminated quoted field on CSV line " +
                                    std::to_string(line_number)};
    }
    fields.push_back(field);
    return fields;
}

std::size_t find_column(const std::vector<std::string>& header,
                        const std::string_view name) {
    std::size_t match = header.size();
    for (std::size_t index = 0; index < header.size(); ++index) {
        if (header[index] == name) {
            if (match != header.size()) {
                throw std::invalid_argument{"duplicate CSV column: " + std::string{name}};
            }
            match = index;
        }
    }
    if (match != header.size()) {
        return match;
    }
    throw std::invalid_argument{"CSV is missing required column: " + std::string{name}};
}

double parse_price(const std::string& text, const std::size_t line_number,
                   const std::string_view column) {
    double value{};
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), value);
    if (error != std::errc{} || end != text.data() + text.size() ||
        !std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument{"invalid " + std::string{column} +
                                    " value on CSV line " +
                                    std::to_string(line_number)};
    }
    return value;
}

void remove_carriage_return(std::string& line) {
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
}

unsigned digit(const char value, const std::size_t line_number) {
    if (value < '0' || value > '9') {
        throw std::invalid_argument{
            "invalid Date value on CSV line " + std::to_string(line_number) +
            "; expected YYYY-MM-DD"};
    }
    return static_cast<unsigned>(value - '0');
}

void validate_date(const std::string& value, const std::size_t line_number) {
    if (value.size() != 10 || value[4] != '-' || value[7] != '-') {
        throw std::invalid_argument{
            "invalid Date value on CSV line " + std::to_string(line_number) +
            "; expected YYYY-MM-DD"};
    }
    const int year_value =
        static_cast<int>(digit(value[0], line_number) * 1000U +
                         digit(value[1], line_number) * 100U +
                         digit(value[2], line_number) * 10U +
                         digit(value[3], line_number));
    const unsigned month_value =
        digit(value[5], line_number) * 10U + digit(value[6], line_number);
    const unsigned day_value =
        digit(value[8], line_number) * 10U + digit(value[9], line_number);
    const std::chrono::year_month_day date{
        std::chrono::year{year_value}, std::chrono::month{month_value},
        std::chrono::day{day_value}};
    if (year_value == 0 || !date.ok()) {
        throw std::invalid_argument{
            "invalid Date value on CSV line " + std::to_string(line_number) +
            "; expected YYYY-MM-DD"};
    }
}

}

PriceHistory read_price_history_csv(std::istream& input,
                                    const std::string_view price_column) {
    if (price_column.empty()) {
        throw std::invalid_argument{"price column must not be empty"};
    }

    std::string line;
    if (!std::getline(input, line)) {
        throw std::invalid_argument{"CSV file is empty"};
    }
    remove_carriage_return(line);
    if (line.starts_with("\xEF\xBB\xBF")) {
        line.erase(0, 3);
    }

    const auto header = parse_row(line, 1);
    const std::size_t date_column = find_column(header, "Date");
    const std::size_t value_column = find_column(header, price_column);

    PriceHistory history;
    std::size_t line_number = 1;
    while (std::getline(input, line)) {
        ++line_number;
        remove_carriage_return(line);
        if (line.empty()) {
            continue;
        }
        const auto fields = parse_row(line, line_number);
        if (fields.size() != header.size()) {
            throw std::invalid_argument{"unexpected column count on CSV line " +
                                        std::to_string(line_number)};
        }
        validate_date(fields[date_column], line_number);
        if (!history.dates.empty() && fields[date_column] <= history.dates.back()) {
            throw std::invalid_argument{
                "CSV dates must be in strictly increasing order on line " +
                std::to_string(line_number)};
        }
        history.dates.push_back(fields[date_column]);
        history.adjusted_closes.push_back(
            parse_price(fields[value_column], line_number, price_column));
    }

    if (input.bad()) {
        throw std::runtime_error{"failed while reading CSV data"};
    }

    if (history.adjusted_closes.empty()) {
        throw std::invalid_argument{"CSV contains no price rows"};
    }
    return history;
}

PriceHistory load_price_history_csv(const std::string& path,
                                    const std::string_view price_column) {
    std::ifstream input{path};
    if (!input) {
        throw std::invalid_argument{"could not open CSV file: " + path};
    }
    return read_price_history_csv(input, price_column);
}

}
