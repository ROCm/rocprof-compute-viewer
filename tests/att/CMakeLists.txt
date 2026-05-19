set(SRC_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src)
set(DATA_DIR ${SRC_DIR}/data)

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)

if(RCV_HAS_TRACE_DECODER)
    add_executable(att_loader_test
        att_loader_test.cpp
        ${DATA_DIR}/input_detector.cpp
        ${DATA_DIR}/trace_decoder_emitter.cpp
        ${DATA_DIR}/record_dispatcher.cpp
        ${DATA_DIR}/record_handlers.cpp
        ${DATA_DIR}/datastore.cpp
        ${DATA_DIR}/shaderdata.cpp
        ${DATA_DIR}/dispatch_resolver.cpp
        ${DATA_DIR}/marker_walker.cpp
        ${SRC_DIR}/wave/othersimd.cpp
        ${SRC_DIR}/code/codeload.cpp
        ${SRC_DIR}/util/custom_layouts.cpp
        ${SRC_DIR}/util/jsonrequest.cpp
        ${SRC_DIR}/config/config.cpp)

    target_include_directories(att_loader_test PRIVATE ${SRC_DIR})
    target_link_libraries(att_loader_test
        GTest::gtest_main
        Qt6::Core Qt6::Network Qt6::Widgets
        rocprof-trace-decoder::rocprof-trace-decoder-static)
    add_test(NAME att_loader_test COMMAND att_loader_test)
    set_tests_properties(att_loader_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
