set(history "${SOURCE_DIR}/examples/sample_prices.csv")
set(metadata "${SOURCE_DIR}/examples/sample_prices.meta")

execute_process(COMMAND "${CLI}" forecast --csv "${history}" --metadata "${metadata}"
    --lookback-days 10 RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "Forecast failed: ${error}")
endif()
foreach(expected IN ITEMS "History:                 2024-01-16 to 2024-01-30"
        "Price observations:      11" "Return observations:     10"
        "Symbol (declared):       SYNTHETIC" "Lookback:                10 trading days")
    string(FIND "${output}" "${expected}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "Forecast output missing: ${expected}")
    endif()
endforeach()

execute_process(COMMAND "${CLI}" forecast --csv "${history}"
    RESULT_VARIABLE status OUTPUT_VARIABLE output)
if(NOT status EQUAL 0 OR NOT output MATCHES "Return observations:     19")
    message(FATAL_ERROR "Default full-history forecast changed")
endif()

execute_process(COMMAND "${CLI}" backtest --csv "${history}" --metadata "${metadata}"
    --lookback-days 10 --horizon-days 3 --step-days 3
    RESULT_VARIABLE status OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT output MATCHES "Symbol \\(declared\\):       SYNTHETIC")
    message(FATAL_ERROR "Backtest provenance failed: ${error}")
endif()

execute_process(COMMAND "${CLI}" forecast --csv "${history}" --lookback-days 20
    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "insufficient history")
    message(FATAL_ERROR "Insufficient history was not rejected")
endif()

file(READ "${metadata}" metadata_text)
foreach(case IN ITEMS column date frequency)
    set(invalid "${metadata_text}")
    if(case STREQUAL "column")
        string(REPLACE "price_column=Adj Close" "price_column=Close" invalid "${invalid}")
        set(expected "does not match")
    elseif(case STREQUAL "date")
        string(REPLACE "retrieved_on=2026-10-07" "retrieved_on=2024-01-01" invalid "${invalid}")
        set(expected "precedes the final history date")
    else()
        string(REPLACE "frequency=daily" "frequency=monthly" invalid "${invalid}")
        set(expected "frequency must be daily")
    endif()
    set(path "${CMAKE_CURRENT_BINARY_DIR}/history_cli_invalid_${case}.meta")
    file(WRITE "${path}" "${invalid}")
    execute_process(COMMAND "${CLI}" forecast --csv "${history}" --metadata "${path}"
        RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
    file(REMOVE "${path}")
    if(status EQUAL 0 OR NOT error MATCHES "${expected}")
        message(FATAL_ERROR "Invalid metadata ${case} was not rejected correctly: ${error}")
    endif()
endforeach()
