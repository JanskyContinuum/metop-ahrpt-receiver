cmake_minimum_required(VERSION 3.20)
project(metop_decoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(metop_core src/cadu_reader.cpp src/cli.cpp src/statistics.cpp src/ccsds_randomizer.cpp)
target_include_directories(metop_core PUBLIC src)
add_executable(metop_decoder src/main.cpp)
target_link_libraries(metop_decoder PRIVATE metop_core)
if(MINGW)
    # Use Windows' native wide command line, including non-ASCII file names.
    target_link_options(metop_decoder PRIVATE -municode)
endif()

function(metop_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive-)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
    endif()
endfunction()
metop_warnings(metop_core)
metop_warnings(metop_decoder)

include(CTest)
if(BUILD_TESTING)
    add_executable(randomizer_tests tests/randomizer_tests.cpp)
    target_link_libraries(randomizer_tests PRIVATE metop_core)
    metop_warnings(randomizer_tests)
    foreach(case IN ITEMS prefix round_trip reset period boundaries)
        add_test(NAME randomizer.${case} COMMAND randomizer_tests ${case})
    endforeach()
    add_executable(cadu_reader_tests tests/cadu_reader_tests.cpp)
    target_link_libraries(cadu_reader_tests PRIVATE metop_core)
    metop_warnings(cadu_reader_tests)
    foreach(case IN ITEMS empty aligned single trailing truncated leading_garbage
            corrupt_marker inserted_byte deleted_byte false_marker unconfirmed
            large_garbage multiple_resync truncated_successor stream_error)
        add_test(NAME reader.${case} COMMAND cadu_reader_tests ${case})
    endforeach()

    foreach(case IN ITEMS help valid unicode_paths limit exact_limit empty garbage truncated recovered
            missing_input missing_out unknown_option invalid_limit zero_limit
            overflow_limit negative_limit missing_value duplicate_option extra_input output_failure
            statistics_failure input_collision)
        add_test(NAME cli.${case} COMMAND ${CMAKE_COMMAND}
            "-DDECODER=$<TARGET_FILE:metop_decoder>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/cli-tests/${case}"
            "-DCASE=${case}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/cli_integration.cmake")
    endforeach()
endif()
