# Marker stack-walker tests. GoogleTest is fetched by the parent
# tests/CMakeLists.txt.

set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(DATA_DIR ${SRC_DIR}/data)

set(TEST_SOURCES
    marker_test.cpp
    ${DATA_DIR}/marker_walker.cpp
    ${DATA_DIR}/sqtt_funcmap_json.cpp)

# ============================================================================
# Normal build (no sanitizers)
# ============================================================================
add_executable(marker_test ${TEST_SOURCES})
target_include_directories(marker_test PRIVATE ${SRC_DIR})
target_link_libraries(marker_test GTest::gtest_main)
add_test(NAME marker_test COMMAND marker_test)

# ============================================================================
# ASAN build (Address Sanitizer)
# ============================================================================
add_executable(marker_test_asan ${TEST_SOURCES})
target_include_directories(marker_test_asan PRIVATE ${SRC_DIR})
target_link_libraries(marker_test_asan GTest::gtest_main)
target_compile_options(marker_test_asan PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(marker_test_asan PRIVATE -fsanitize=address)
add_test(NAME marker_test_asan COMMAND marker_test_asan)

# ============================================================================
# UBSAN build (Undefined Behavior Sanitizer)
# ============================================================================
add_executable(marker_test_ubsan ${TEST_SOURCES})
target_include_directories(marker_test_ubsan PRIVATE ${SRC_DIR})
target_link_libraries(marker_test_ubsan GTest::gtest_main)
target_compile_options(marker_test_ubsan PRIVATE -fsanitize=undefined -fno-omit-frame-pointer -g)
target_link_options(marker_test_ubsan PRIVATE -fsanitize=undefined)
add_test(NAME marker_test_ubsan COMMAND marker_test_ubsan)
