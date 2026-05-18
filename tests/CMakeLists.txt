cmake_minimum_required(VERSION 3.14)
project(RcvZoomTests)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RCV_ENABLE_EXTRA_LOGGING "Enable extra diagnostic logging" OFF)
add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/../src/util/diagnostic_log.h")
if(RCV_ENABLE_EXTRA_LOGGING)
    add_compile_definitions(RCV_ENABLE_EXTRA_LOGGING)
endif()

if(DEFINED TRACE_DECODER_ROOT)
    find_package(rocprof-trace-decoder REQUIRED CONFIG
        PATHS ${TRACE_DECODER_ROOT}
        PATH_SUFFIXES source lib/cmake/rocprof-trace-decoder
        NO_DEFAULT_PATH)
    set(RCV_HAS_TRACE_DECODER ON)
    add_compile_definitions(RCV_HAS_TRACE_DECODER)
else()
    set(RCV_HAS_TRACE_DECODER OFF)
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
