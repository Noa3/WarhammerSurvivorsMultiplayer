if(NOT DEFINED TEST_BINARY OR NOT EXISTS "${TEST_BINARY}")
    message(FATAL_ERROR "A built input_driver_test executable is required")
endif()
execute_process(
    COMMAND "${TEST_BINARY}" --pending-destructor
    RESULT_VARIABLE result
    OUTPUT_VARIABLE output
    ERROR_VARIABLE errors
    TIMEOUT 10
)
if(NOT "${result}" STREQUAL "42")
    message(FATAL_ERROR "Pending driver destruction did not preserve ownership before termination: expected exit 42, got ${result}. ${output} ${errors}")
endif()
message(STATUS "PASS pending driver destruction refuses to discard a live backend (exact exit 42)")
