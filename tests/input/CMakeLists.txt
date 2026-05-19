set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(DATA_DIR ${SRC_DIR}/data)

add_executable(input_detector_test
    input_detector_test.cpp
    ${DATA_DIR}/input_detector.cpp)

target_include_directories(input_detector_test PRIVATE ${SRC_DIR})
target_link_libraries(input_detector_test GTest::gtest_main)
add_test(NAME input_detector_test COMMAND input_detector_test)
