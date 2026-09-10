# Exercise the integrated viewer against production code, not UI stubs. A
# separate directory keeps Qt's generated mainwindow UI header target-local.
find_package(Qt6 REQUIRED COMPONENTS Network OpenGLWidgets)
file(GLOB_RECURSE ISA_VIEWER_SOURCES CONFIGURE_DEPENDS ${SRC_DIR}/*.cpp)
list(FILTER ISA_VIEWER_SOURCES EXCLUDE REGEX "/main\\.cpp$")
file(READ "${SRC_DIR}/../LICENSE" LICENSE_FILE_CONTENT)
set(LICENSE_HEADER_FILE "${SRC_DIR}/collection/license.h")
configure_file(${SRC_DIR}/collection/license_data.cpp.in generated/license_data.cpp @ONLY)
# The standalone tests project has no release version.
set(PROJECT_VERSION_MAJOR 0)
set(PROJECT_VERSION_MINOR 0)
set(PROJECT_VERSION_PATCH 0)
configure_file(${SRC_DIR}/util/version.h.in generated/util/version.h @ONLY)
add_executable(isa_viewer_test ../isa_viewer_test.cpp ${ISA_VIEWER_SOURCES}
    ${CMAKE_CURRENT_BINARY_DIR}/generated/license_data.cpp)
set_target_properties(isa_viewer_test PROPERTIES AUTOUIC ON AUTOUIC_SEARCH_PATHS ${SRC_DIR})
target_include_directories(isa_viewer_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated ${SRC_DIR})
target_link_libraries(isa_viewer_test GTest::gtest Qt6::Widgets Qt6::Test Qt6::Network Qt6::OpenGLWidgets)
if(RCV_HAS_TRACE_DECODER)
    target_link_libraries(isa_viewer_test rocprof-trace-decoder::rocprof-trace-decoder-static)
endif()
rcv_enable_asan(isa_viewer_test)
add_test(NAME isa_viewer_test COMMAND isa_viewer_test)
set_tests_properties(isa_viewer_test PROPERTIES ENVIRONMENT "ASAN_OPTIONS=detect_leaks=0;QT_QPA_PLATFORM=offscreen")
