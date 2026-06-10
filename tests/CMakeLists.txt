cmake_minimum_required(VERSION 3.14)
project(RcvZoomTests LANGUAGES C CXX) # C is also needed by some decoder backend dependency checks.

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RCV_ENABLE_EXTRA_LOGGING "Enable extra diagnostic logging" OFF)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/../src/util/diagnostic_log.h>)
else()
    add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/../src/util/diagnostic_log.h")
endif()
if(RCV_ENABLE_EXTRA_LOGGING)
    add_compile_definitions(RCV_ENABLE_EXTRA_LOGGING)
endif()

if(MSVC)
    set(_rcv_sanitizer_tests_default OFF)
else()
    set(_rcv_sanitizer_tests_default ON)
endif()
option(RCV_ENABLE_SANITIZER_TESTS "Build sanitizer-instrumented test targets" ${_rcv_sanitizer_tests_default})
if(MSVC AND RCV_ENABLE_SANITIZER_TESTS)
    message(WARNING "MSVC sanitizer test variants are disabled; GoogleTest must be built with matching ASan STL annotations.")
    set(RCV_ENABLE_SANITIZER_TESTS
        OFF
        CACHE BOOL "Build sanitizer-instrumented test targets" FORCE)
endif()

function(rcv_enable_asan target)
    if(NOT RCV_ENABLE_SANITIZER_TESTS)
        return()
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=address)
endfunction()

function(rcv_enable_ubsan target)
    if(NOT RCV_ENABLE_SANITIZER_TESTS)
        return()
    endif()
    target_compile_options(${target} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer -g)
    target_link_options(${target} PRIVATE -fsanitize=undefined)
endfunction()

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/../cmake)
include(FetchTraceDecoder)
rcv_fetch_trace_decoder()
if(RCV_HAS_TRACE_DECODER)
    add_compile_definitions(RCV_HAS_TRACE_DECODER)
endif()

# When the decoder was fetched, extract all of its bundled datasets so the
# att_loader_test can cross-check RCV's per-instruction aggregates against
# the decoder's golden CSVs for every available trace.
if(RCV_HAS_TRACE_DECODER AND RCV_TRACE_DECODER_SRC_DIR)
    rcv_extract_decoder_test_data(
        "${RCV_TRACE_DECODER_SRC_DIR}"
        "${CMAKE_BINARY_DIR}/decoder_test_data")
endif()

# Output all test binaries to build/bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Fetch GoogleTest (shared across all test modules)
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Add test subdirectories
add_subdirectory(att)
add_subdirectory(input)
add_subdirectory(counters)
add_subdirectory(editor)
add_subdirectory(latency)
add_subdirectory(markers)
add_subdirectory(widgets)
