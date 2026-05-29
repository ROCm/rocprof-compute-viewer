# Derived Counter Tests GoogleTest is fetched by the parent tests/CMakeLists.txt

# Path to the src directory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(GRAPHICS_DIR ${SRC_DIR}/analysis)

set(TEST_SOURCES derived_counter_test.cpp ${GRAPHICS_DIR}/derived_counter.cpp)

# ============================================================================
# Normal build (no sanitizers)
# ============================================================================
add_executable(derived_counter_test ${TEST_SOURCES})
target_include_directories(derived_counter_test PRIVATE ${GRAPHICS_DIR} ${SRC_DIR})
target_link_libraries(derived_counter_test GTest::gtest_main)
add_test(NAME derived_counter_test COMMAND derived_counter_test)

if(RCV_ENABLE_SANITIZER_TESTS)
    # ============================================================================
    # ASAN build (Address Sanitizer)
    # ============================================================================
    add_executable(derived_counter_test_asan ${TEST_SOURCES})
    target_include_directories(derived_counter_test_asan PRIVATE ${GRAPHICS_DIR} ${SRC_DIR})
    target_link_libraries(derived_counter_test_asan GTest::gtest_main)
    rcv_enable_asan(derived_counter_test_asan)
    add_test(NAME derived_counter_test_asan COMMAND derived_counter_test_asan)

    # ============================================================================
    # UBSAN build (Undefined Behavior Sanitizer)
    # ============================================================================
    add_executable(derived_counter_test_ubsan ${TEST_SOURCES})
    target_include_directories(derived_counter_test_ubsan PRIVATE ${GRAPHICS_DIR} ${SRC_DIR})
    target_link_libraries(derived_counter_test_ubsan GTest::gtest_main)
    rcv_enable_ubsan(derived_counter_test_ubsan)
    add_test(NAME derived_counter_test_ubsan COMMAND derived_counter_test_ubsan)
endif()
