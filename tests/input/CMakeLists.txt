set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(DATA_DIR ${SRC_DIR}/data)
set(UTIL_DIR ${SRC_DIR}/util)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets)

add_executable(input_detector_test
    input_detector_test.cpp
    ${DATA_DIR}/input_detector.cpp)

target_include_directories(input_detector_test PRIVATE ${SRC_DIR})
target_link_libraries(input_detector_test GTest::gtest_main)
add_test(NAME input_detector_test COMMAND input_detector_test)

add_executable(datastore_test
    datastore_test.cpp
    ${DATA_DIR}/datastore.cpp
    ${DATA_DIR}/dispatch_resolver.cpp
    ${DATA_DIR}/shaderdata.cpp
    ${DATA_DIR}/marker_walker.cpp
    ${UTIL_DIR}/jsonrequest.cpp)

target_include_directories(datastore_test PRIVATE ${SRC_DIR})
target_link_libraries(datastore_test GTest::gtest_main Qt6::Core Qt6::Network)
if(RCV_HAS_TRACE_DECODER)
    target_link_libraries(datastore_test rocprof-trace-decoder::rocprof-trace-decoder-static)
endif()
add_test(NAME datastore_test COMMAND datastore_test)

add_executable(json_emitter_test
    json_emitter_test.cpp
    ${DATA_DIR}/json_emitter.cpp
    ${DATA_DIR}/record_dispatcher.cpp
    ${DATA_DIR}/record_handlers.cpp
    ${DATA_DIR}/datastore.cpp
    ${DATA_DIR}/dispatch_resolver.cpp
    ${DATA_DIR}/shaderdata.cpp
    ${DATA_DIR}/marker_walker.cpp
    ${DATA_DIR}/sqtt_funcmap_json.cpp
    ${SRC_DIR}/code/codeload.cpp
    ${SRC_DIR}/wave/othersimd.cpp
    ${SRC_DIR}/config/config.cpp
    ${UTIL_DIR}/jsonrequest.cpp)

target_include_directories(json_emitter_test PRIVATE ${SRC_DIR})
target_link_libraries(json_emitter_test GTest::gtest_main Qt6::Core Qt6::Network Qt6::Widgets)
if(RCV_HAS_TRACE_DECODER)
    target_link_libraries(json_emitter_test rocprof-trace-decoder::rocprof-trace-decoder-static)
endif()
add_test(NAME json_emitter_test COMMAND json_emitter_test)
