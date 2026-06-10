# Widget Tests GoogleTest is fetched by the parent tests/CMakeLists.txt

# Find Qt6 components needed for widget testing
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# Path to the src directory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(UTIL_DIR ${SRC_DIR}/util)
set(COLLECTION_DIR ${SRC_DIR}/collection)
set(GRAPHICS_DIR ${SRC_DIR}/graphics)

# Enable Qt MOC for headers with Q_OBJECT
set(CMAKE_AUTOMOC ON)

# ASAN suppression file
set(ASAN_SUPPRESSIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../asan_suppressions.txt")

# ============================================================================
# Color Tests - Color utility class with arithmetic and clamping
# ============================================================================
add_executable(color_test color_test.cpp ${UTIL_DIR}/highlight.cpp)
target_include_directories(color_test PRIVATE ${UTIL_DIR} ${SRC_DIR})
target_link_libraries(color_test GTest::gtest_main Qt6::Widgets)
rcv_enable_asan(color_test)
add_test(NAME color_test COMMAND color_test)
set_tests_properties(color_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# ============================================================================
# PlotCurve Tests - LOD curve data structures and PlotCurve with LOD generation
# ============================================================================
add_executable(plotcurve_test plotcurve_test.cpp)
target_include_directories(plotcurve_test PRIVATE ${SRC_DIR})
target_link_libraries(plotcurve_test GTest::gtest Qt6::Widgets)
rcv_enable_asan(plotcurve_test)
add_test(NAME plotcurve_test COMMAND plotcurve_test)
set_tests_properties(plotcurve_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# ============================================================================
# TextElement Tests - Text line rendering with caching and hover states
# ============================================================================
add_executable(textelement_test textelement_test.cpp)
target_include_directories(textelement_test PRIVATE ${SRC_DIR})
target_link_libraries(textelement_test GTest::gtest Qt6::Widgets)
rcv_enable_asan(textelement_test)
add_test(NAME textelement_test COMMAND textelement_test)
set_tests_properties(textelement_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")

# ============================================================================
# Wave Utils Tests - Pure utility functions from wave/token and sourcefile No Qt dependencies - tests mip shifting,
# token sizing, path parsing
# ============================================================================
add_executable(wave_utils_test wave_utils_test.cpp)
target_include_directories(wave_utils_test PRIVATE ${SRC_DIR})
target_link_libraries(wave_utils_test GTest::gtest_main)
rcv_enable_asan(wave_utils_test)
add_test(NAME wave_utils_test COMMAND wave_utils_test)
set_tests_properties(wave_utils_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
