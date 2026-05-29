cmake_minimum_required(VERSION 3.14)
project(RcvZoomTests LANGUAGES C CXX) # C needed for LLVM's check_c_source_compiles via the decoder

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RCV_ENABLE_EXTRA_LOGGING "Enable extra diagnostic logging" OFF)
add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/../src/util/diagnostic_log.h")
if(RCV_ENABLE_EXTRA_LOGGING)
    add_compile_definitions(RCV_ENABLE_EXTRA_LOGGING)
endif()

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
