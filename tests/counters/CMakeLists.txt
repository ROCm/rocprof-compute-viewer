# Derived Counter Tests GoogleTest is fetched by the parent tests/CMakeLists.txt

# Path to the src directory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(GRAPHICS_DIR ${SRC_DIR}/analysis)

set(TEST_SOURCES derived_counter_test.cpp ${GRAPHICS_DIR}/derived_counter.cpp)

# ============================================================================
# Normal build (no sanitizers)
# ============================================================================
add_executable(derived_counter_test ${TEST_SOURCES})
target_include_directories(derived_counter_test PRIVATE ${GRAPHICS_DIR})
target_link_libraries(derived_counter_test GTest::gtest_main)
add_test(NAME derived_counter_test COMMAND derived_counter_test)

# ============================================================================
# ASAN build (Address Sanitizer)
# ============================================================================
add_executable(derived_counter_test_asan ${TEST_SOURCES})
target_include_directories(derived_counter_test_asan PRIVATE ${GRAPHICS_DIR})
target_link_libraries(derived_counter_test_asan GTest::gtest_main)
target_compile_options(derived_counter_test_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(derived_counter_test_asan PRIVATE -fsanitize=address)
add_test(NAME derived_counter_test_asan COMMAND derived_counter_test_asan)

# ============================================================================
# UBSAN build (Undefined Behavior Sanitizer)
# ============================================================================
add_executable(derived_counter_test_ubsan ${TEST_SOURCES})
target_include_directories(derived_counter_test_ubsan PRIVATE ${GRAPHICS_DIR})
target_link_libraries(derived_counter_test_ubsan GTest::gtest_main)
target_compile_options(derived_counter_test_ubsan PRIVATE -fsanitize=undefined -fno-omit-frame-pointer -g)
target_link_options(derived_counter_test_ubsan PRIVATE -fsanitize=undefined)
add_test(NAME derived_counter_test_ubsan COMMAND derived_counter_test_ubsan)
