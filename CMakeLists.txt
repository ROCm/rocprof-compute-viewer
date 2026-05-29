cmake_minimum_required(VERSION 3.16...3.22)
project(
    rocprof-compute-viewer
    VERSION 0.2.0
    LANGUAGES C CXX) # C is also needed by some decoder backend dependency checks.

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(Threads)
set(THREADS_PREFER_PTHREAD_FLAG ON)

if(NOT DEFINED QT_VERSION_MAJOR)
    set(QT_VERSION_MAJOR 6)
endif()

if(NOT DEFINED QT_VERSION_MINOR)
    set(QT_VERSION_MINOR 8)
endif()

if(${QT_VERSION_MAJOR} EQUAL 6)
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network OpenGLWidgets)
    qt_standard_project_setup()
else()
    find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Gui Widgets Network)
endif()

option(RCV_DISABLE_OPENGL "" OFF)
if(RCV_DISABLE_OPENGL)
    add_compile_definitions(RCV_DISABLE_OPENGL)
endif(RCV_DISABLE_OPENGL)

option(RCV_ENABLE_EXTRA_LOGGING "Enable extra diagnostic logging" OFF)
if(MSVC)
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/FI${CMAKE_CURRENT_SOURCE_DIR}/src/util/diagnostic_log.h>)
else()
    add_compile_options(-include "${CMAKE_CURRENT_SOURCE_DIR}/src/util/diagnostic_log.h")
endif()
if(RCV_ENABLE_EXTRA_LOGGING)
    add_compile_definitions(RCV_ENABLE_EXTRA_LOGGING)
endif()

# ----------------------------------------------------------------------------
# Trace-decoder + disassembly support.
#
# Three modes, evaluated in this order:
#   1. TRACE_DECODER_ROOT — path to an existing decoder build tree / install
#      prefix. RCV consumes the decoder via its CMake package config; LLVM
#      (or amd_comgr) and all transitive include dirs / link libs / compile
#      defs propagate automatically through the namespaced target.
#   2. RCV_FETCH_TRACE_DECODER — fetch the decoder from
#      github.com/ROCm/rocm-systems via a sparse checkout of
#      projects/rocprof-trace-decoder, build it in an isolated sub-build, and
#      prefer the amd_comgr disassembly backend when a ROCm SDK is available.
#   3. Neither — trace-decoder support is disabled (JSON input only).
# ----------------------------------------------------------------------------
# Default the build type before fetching the decoder so the decoder sub-build
# receives a real -DCMAKE_BUILD_TYPE rather than an empty one.
if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)
include(FetchTraceDecoder)
rcv_fetch_trace_decoder()
set(RCV_ENABLE_TRACE_DECODER ${RCV_HAS_TRACE_DECODER})
if(RCV_ENABLE_TRACE_DECODER)
    add_compile_definitions(RCV_HAS_TRACE_DECODER)
endif()

include_directories(src)
include_directories(${CMAKE_CURRENT_BINARY_DIR}/src)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" LICENSE_FILE_CONTENT)
set(LICENSE_HEADER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/collection/license.h")
configure_file("src/collection/license_data.cpp.in" "src/collection/license_data.cpp" @ONLY)
set_property(
    DIRECTORY
    APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS src/collection/license_data.cpp)

# Configure version header
configure_file("src/util/version.h.in" "src/util/version.h" @ONLY)

file(
    GLOB
    RCVIEWER_SOURCE_FILES
    src/main.cpp
    src/mainwindow.cpp
    src/button/*.cpp
    src/code/*.cpp
    src/collection/*.cpp
    src/collection/flamegraph/*.cpp
    src/data/*.cpp
    src/data/waitcnt/*.cpp
    src/graphics/*.cpp
    src/graphics/container/*.cpp
    src/util/*.cpp
    src/wave/*.cpp
    src/config/*.cpp
    src/summary/*.cpp
    src/analysis/*.cpp
    src/mainwindow.ui
    icon.qrc
    ${CMAKE_CURRENT_BINARY_DIR}/src/collection/license_data.cpp)

set(QT_V Qt${QT_VERSION_MAJOR})

if(${QT_VERSION_MAJOR} EQUAL 6 AND ${QT_VERSION_MINOR} GREATER_EQUAL 8)

    if(WIN32 AND MSVC)
        list(APPEND RCVIEWER_SOURCE_FILES icon.rc)
    endif()
    qt_add_executable(${PROJECT_NAME} ${RCVIEWER_SOURCE_FILES})

    set(deploy_tool_options_arg "")

    if(APPLE)
        # App bundles on macOS have an .app suffix
        set(executable_path "$<TARGET_FILE_NAME:${PROJECT_NAME}>.app")
        set_target_properties(${PROJECT_NAME} PROPERTIES MACOSX_BUNDLE TRUE)
    elseif(WIN32)
        set(executable_path "$<TARGET_FILE:${PROJECT_NAME}>")
        set_target_properties(${PROJECT_NAME} PROPERTIES WIN32_EXECUTABLE TRUE RC_FILE icon.rc)
        set(deploy_tool_options_arg "--no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --release")
    else()
        set(executable_path "$<TARGET_FILE:${PROJECT_NAME}>")
    endif()

    # Generate a deployment script to be executed at install time

    qt_generate_deploy_script(
        TARGET
        ${PROJECT_NAME}
        OUTPUT_SCRIPT
        deploy_script
        CONTENT
        "
include(\"${QT_DEPLOY_SUPPORT}\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"${executable_path}\"
    DEPLOY_TOOL_OPTIONS ${deploy_tool_options_arg}
    GENERATE_QT_CONF
    VERBOSE)
")

    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${QT_V}::Core ${QT_V}::Gui ${QT_V}::Widgets
                                                  ${QT_V}::Network ${QT_V}::OpenGLWidgets)

    if(RCV_ENABLE_TRACE_DECODER)
        # Namespaced target carries LLVM (or comgr) transitively via PUBLIC
        # link/include/define interface — nothing else to wire up here.
        target_link_libraries(${PROJECT_NAME} PRIVATE rocprof-trace-decoder::rocprof-trace-decoder-static)
    endif()

    install(
        TARGETS ${PROJECT_NAME}
        BUNDLE DESTINATION .
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
    install(SCRIPT ${deploy_script})

    set(RCVIEW_CONFIG_DESTINATION ${CMAKE_INSTALL_PREFIX}/share/rocprof-compute-viewer)

    set(CPACK_PACKAGE_NAME "Rocprof-Compute-Viewer")
    set(CPACK_PACKAGE_VENDOR "Advanced Micro Devices, Inc.")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "ROCprof Compute Viewer")
    set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
    set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
    set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
    set(CPACK_PACKAGE_CONTACT "ROCm Profiler Support <dl.ROCm-Profiler.support@amd.com>")
    set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/NOTICES")
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "ROCprof-Compute-Viewer")

    if(DEFINED ENV{CPACK_PACKAGE_FILE_NAME})
        set(CPACK_PACKAGE_FILE_NAME $ENV{CPACK_PACKAGE_FILE_NAME})
    endif()

    if(WIN32)
        set(CPACK_GENERATOR NSIS)
        set(CPACK_NSIS_CREATE_ICONS_EXTRA
            "CreateShortCut '$DESKTOP\\\\ROCprof Compute Viewer.lnk' '$INSTDIR\\\\bin\\\\rocprof-compute-viewer.exe'")
        set(CPACK_NSIS_MUI_ICON "${CMAKE_CURRENT_SOURCE_DIR}\\\\amd.ico")
        set(CPACK_NSIS_INSTALLED_ICON_NAME "$INSTDIR\\\\bin\\\\rocprof-compute-viewer.exe")
        set(CPACK_NSIS_PACKAGE_NAME "ROCprof-Compute-Viewer")
        set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES")
    elseif(APPLE)
        set(CPACK_GENERATOR STGZ)
    else()
        set(CPACK_GENERATOR "STGZ" "DEB" "RPM")
    endif()

    set(CPACK_PACKAGE_VERSION
        "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}")

    include(CPack)
else()

    add_executable(${PROJECT_NAME} ${RCVIEWER_SOURCE_FILES})

    if(${QT_VERSION_MAJOR} EQUAL 6)
        set(QT_EXTRA_WIDGET ${QT_V}::OpenGLWidgets)
    endif()

    target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${QT_V}::Network ${QT_V}::Core ${QT_V}::Gui
                                                  ${QT_V}::Widgets ${QT_EXTRA_WIDGET})

    if(RCV_ENABLE_TRACE_DECODER)
        target_link_libraries(${PROJECT_NAME} PRIVATE rocprof-trace-decoder::rocprof-trace-decoder-static)
    endif()

endif()

# clang-format target. Prefer clang-format-14 explicitly so an older generic
# `clang-format` in PATH, or a stale cached CLANG_FORMAT value, is not used.
unset(CLANG_FORMAT)
find_program(CLANG_FORMAT_14 NAMES clang-format-14)
if(CLANG_FORMAT_14)
    set(CLANG_FORMAT ${CLANG_FORMAT_14})
else()
    find_program(CLANG_FORMAT_GENERIC NAMES clang-format)
    set(CLANG_FORMAT ${CLANG_FORMAT_GENERIC})
endif()

if(CLANG_FORMAT)
    execute_process(
        COMMAND ${CLANG_FORMAT} --version
        OUTPUT_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
        ERROR_VARIABLE CLANG_FORMAT_VERSION_OUTPUT
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_STRIP_TRAILING_WHITESPACE)
    string(REGEX MATCH "version ([0-9]+)" _CLANG_FORMAT_VERSION_MATCH "${CLANG_FORMAT_VERSION_OUTPUT}")
    set(CLANG_FORMAT_VERSION_MAJOR "")
    if(_CLANG_FORMAT_VERSION_MATCH)
        set(CLANG_FORMAT_VERSION_MAJOR ${CMAKE_MATCH_1})
    endif()

    if(NOT CLANG_FORMAT_VERSION_MAJOR STREQUAL "14")
        message(WARNING "clang-format target disabled: expected clang-format 14, found `${CLANG_FORMAT_VERSION_OUTPUT}`")
        set(CLANG_FORMAT "")
    endif()
endif()

if(CLANG_FORMAT)
    message(STATUS "Using clang-format 14: ${CLANG_FORMAT}")
    file(GLOB_RECURSE ALL_CXX_SOURCE_FILES
        ${CMAKE_SOURCE_DIR}/src/*.cpp
        ${CMAKE_SOURCE_DIR}/src/*.h
        ${CMAKE_SOURCE_DIR}/src/*.hpp)
    list(FILTER ALL_CXX_SOURCE_FILES EXCLUDE REGEX "/json/")
    add_custom_target(format
        COMMAND ${CLANG_FORMAT} -i ${ALL_CXX_SOURCE_FILES}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMENT "Running clang-format on source files"
        VERBATIM)
endif()
