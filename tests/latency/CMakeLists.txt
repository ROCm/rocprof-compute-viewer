# Latency Analysis Tests
# GoogleTest is provided by the parent tests/CMakeLists.txt.

# Path to the src directory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(ANALYSIS_DIR ${SRC_DIR}/analysis)

set(TEST_SOURCES latency_test.cpp ${ANALYSIS_DIR}/latency.cpp)

# Build with Address Sanitizer
add_executable(latency_test ${TEST_SOURCES})
target_include_directories(latency_test PRIVATE ${ANALYSIS_DIR})
target_link_libraries(latency_test GTest::gtest_main)
rcv_enable_asan(latency_test)
add_test(NAME latency_test COMMAND latency_test)

# CLI tool for comparing with Python
add_executable(latency_cli latency_cli.cpp ${ANALYSIS_DIR}/latency.cpp)
target_include_directories(latency_cli PRIVATE ${ANALYSIS_DIR} ${SRC_DIR})
target_compile_features(latency_cli PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(latency_cli PRIVATE /O2)
else()
    target_compile_options(latency_cli PRIVATE -g -O2)
endif()
