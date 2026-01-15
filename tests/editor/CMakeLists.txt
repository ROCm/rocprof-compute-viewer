# DerivedCounterEditor Tests GoogleTest is fetched by the parent tests/CMakeLists.txt

# Find Qt6 components needed for widget testing
find_package(Qt6 REQUIRED COMPONENTS Widgets Test)

# Path to the src directory
set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(COLLECTION_DIR ${SRC_DIR}/collection)

# Enable Qt MOC for the editor header (it has Q_OBJECT)
set(CMAKE_AUTOMOC ON)

set(TEST_SOURCES derivedcountereditor_test.cpp ${COLLECTION_DIR}/derivedcountereditor.cpp)

set(TEST_HEADERS ${COLLECTION_DIR}/derivedcountereditor.h)

# Build with Address Sanitizer enabled
add_executable(derivedcountereditor_test ${TEST_SOURCES} ${TEST_HEADERS})
target_include_directories(derivedcountereditor_test PRIVATE ${COLLECTION_DIR} ${SRC_DIR})
target_link_libraries(derivedcountereditor_test GTest::gtest_main Qt6::Widgets Qt6::Test)
target_compile_options(derivedcountereditor_test PRIVATE -fsanitize=address -fno-omit-frame-pointer -g)
target_link_options(derivedcountereditor_test PRIVATE -fsanitize=address)

# Use suppression file to ignore known Qt/Wayland false positive leaks
set(ASAN_SUPPRESSIONS_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../asan_suppressions.txt")
add_test(NAME derivedcountereditor_test COMMAND derivedcountereditor_test)
set_tests_properties(derivedcountereditor_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0")
