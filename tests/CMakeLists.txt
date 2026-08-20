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

# Use a local/installed GoogleTest when one is available, and fall back to
# FetchContent for builds that do not provide one.
set(gtest_force_shared_crt
    ON
    CACHE BOOL "" FORCE)
set(RCV_GOOGLETEST_SOURCE_DIR
    ""
    CACHE PATH "Path to a local GoogleTest source checkout")

function(rcv_add_gtest_target_alias alias target)
    if(TARGET ${target} AND NOT TARGET ${alias})
        add_library(${alias} INTERFACE IMPORTED)
        set_property(TARGET ${alias} PROPERTY INTERFACE_LINK_LIBRARIES ${target})
    endif()
endfunction()

function(rcv_normalize_gtest_targets)
    rcv_add_gtest_target_alias(GTest::gtest GTest::GTest)
    rcv_add_gtest_target_alias(GTest::gtest_main GTest::Main)
    rcv_add_gtest_target_alias(GTest::gtest gtest)
    rcv_add_gtest_target_alias(GTest::gtest_main gtest_main)
endfunction()

function(rcv_gtest_targets_found out_var)
    if(TARGET GTest::gtest AND TARGET GTest::gtest_main)
        set(${out_var}
            TRUE
            PARENT_SCOPE)
    else()
        set(${out_var}
            FALSE
            PARENT_SCOPE)
    endif()
endfunction()

find_package(GTest CONFIG QUIET)
rcv_normalize_gtest_targets()
rcv_gtest_targets_found(_rcv_gtest_found)
if(NOT _rcv_gtest_found)
    find_package(GTest MODULE QUIET)
    rcv_normalize_gtest_targets()
    rcv_gtest_targets_found(_rcv_gtest_found)
endif()

if(NOT _rcv_gtest_found)
    set(_rcv_gtest_source_candidates)
    if(RCV_GOOGLETEST_SOURCE_DIR)
        list(APPEND _rcv_gtest_source_candidates "${RCV_GOOGLETEST_SOURCE_DIR}")
    endif()
    foreach(_rcv_gtest_env_var IN ITEMS GOOGLETEST_ROOT GTEST_ROOT)
        if(DEFINED ENV{${_rcv_gtest_env_var}} AND NOT "$ENV{${_rcv_gtest_env_var}}" STREQUAL "")
            list(APPEND _rcv_gtest_source_candidates "$ENV{${_rcv_gtest_env_var}}"
                 "$ENV{${_rcv_gtest_env_var}}/googletest")
        endif()
    endforeach()
    list(
        APPEND
        _rcv_gtest_source_candidates
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/googletest"
        "${CMAKE_CURRENT_SOURCE_DIR}/../external/gtest"
        "/usr/src/googletest"
        "/usr/src/gtest")

    foreach(_rcv_gtest_source_dir IN LISTS _rcv_gtest_source_candidates)
        get_filename_component(_rcv_gtest_source_dir "${_rcv_gtest_source_dir}" ABSOLUTE)
        if(NOT EXISTS "${_rcv_gtest_source_dir}/CMakeLists.txt")
            continue()
        endif()
        if(NOT EXISTS "${_rcv_gtest_source_dir}/googletest/include/gtest/gtest.h"
           AND NOT EXISTS "${_rcv_gtest_source_dir}/include/gtest/gtest.h")
            continue()
        endif()

        message(STATUS "Using local GoogleTest source: ${_rcv_gtest_source_dir}")
        add_subdirectory("${_rcv_gtest_source_dir}" "${CMAKE_BINARY_DIR}/googletest-local" EXCLUDE_FROM_ALL)
        rcv_normalize_gtest_targets()
        rcv_gtest_targets_found(_rcv_gtest_found)
        if(_rcv_gtest_found)
            break()
        endif()
    endforeach()
endif()

if(_rcv_gtest_found)
    message(STATUS "Using local/installed GoogleTest")
else()
    message(STATUS "GoogleTest not found locally; fetching v1.14.0")
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.14.0)
    FetchContent_MakeAvailable(googletest)
endif()

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
