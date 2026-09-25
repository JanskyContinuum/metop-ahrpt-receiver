cmake_minimum_required(VERSION 3.20)
project(metop_decoder VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(metop_core src/cadu_reader.cpp src/cli.cpp src/statistics.cpp
    src/ccsds_randomizer.cpp src/vcdu.cpp src/mpdu.cpp src/frame_inspector.cpp
    src/space_packet.cpp src/packet_reassembler.cpp)
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
    add_executable(packet_tests tests/packet_tests.cpp)
    target_link_libraries(packet_tests PRIVATE metop_core)
    metop_warnings(packet_tests)
    foreach(case IN ITEMS header lengths three_mpdus boundaries split_headers orphan
            split_prefix segmented_stream idle_between idle_packet gap gap_idle gap_boundary counter_wrap streams
            duplicate duplicate_after_invalid_version duplicate_after_invalid_size
            duplicate_after_alignment_loss backward mismatch continuation_overrun
            invalid_header invalid_frame finish maximum)
        add_test(NAME packet.${case} COMMAND packet_tests ${case})
    endforeach()
    foreach(case IN ITEMS complete limit partial gap resync duplicate_invalid duplicate_resync collision output_failure)
        add_test(NAME packet_cli.${case} COMMAND ${CMAKE_COMMAND}
            "-DDECODER=$<TARGET_FILE:metop_decoder>"
            "-DFIXTURE=$<TARGET_FILE:packet_tests>"
            "-DTEST_ROOT=${CMAKE_CURRENT_BINARY_DIR}/packet-cli-tests/${case}"
            "-DCASE=${case}" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/packet_cli.cmake")
    endforeach()
    add_executable(frame_tests tests/frame_tests.cpp)
    target_link_libraries(frame_tests PRIVATE metop_core)
    metop_warnings(frame_tests)
    foreach(case IN ITEMS vcdu_header vcdu_lengths mpdu_lengths fhp_0 fhp_100 fhp_881
            fhp_882 fhp_2045 fhp_2046 fhp_2047 counter_wrap counter_events counter_streams inspector)
        add_test(NAME frame.${case} COMMAND frame_tests ${case})
    endforeach()
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
            no_rs no_rs_invalid no_rs_idle no_rs_log_collision no_rs_duplicate
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
