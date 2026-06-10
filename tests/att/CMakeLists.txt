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
        ${DATA_DIR}/sqtt_funcmap_json.cpp
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
    set(_att_env "QT_QPA_PLATFORM=offscreen")
    if(RCV_DECODER_TEST_DATA_DIR)
        list(APPEND _att_env "RCV_ATT_TEST_ROOT=${RCV_DECODER_TEST_DATA_DIR}")
    endif()
    if(RCV_TRACE_DECODER_SRC_DIR AND EXISTS "${RCV_TRACE_DECODER_SRC_DIR}/test/control")
        list(APPEND _att_env "RCV_DECODER_GOLDEN_ROOT=${RCV_TRACE_DECODER_SRC_DIR}/test/control")
    endif()
    set_tests_properties(att_loader_test PROPERTIES ENVIRONMENT "${_att_env}")
endif()
