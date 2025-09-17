cmake_minimum_required(VERSION 3.16...3.22)
project(rocprof-compute-viewer VERSION 0.1.3 LANGUAGES CXX)

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

OPTION(RCV_DISABLE_OPENGL "" OFF)
IF(RCV_DISABLE_OPENGL)
    add_compile_definitions(RCV_DISABLE_OPENGL)
ENDIF(RCV_DISABLE_OPENGL)

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

include_directories(src)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE" LICENSE_FILE_CONTENT)
set(LICENSE_HEADER_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/collection/license.h")
configure_file("src/collection/license_data.cpp.in" "src/collection/license_data.cpp" @ONLY)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS src/collection/license_data.cpp)

file(GLOB RCVIEWER_SOURCE_FILES
    src/main.cpp
    src/mainwindow.cpp
    src/button/*.cpp
    src/code/*.cpp
    src/collection/*.cpp
    src/data/*.cpp
    src/graphics/*.cpp
    src/graphics/container/*.cpp
    src/util/*.cpp
    src/wave/*.cpp
    src/config/*.cpp
    src/summary/*.cpp
    src/mainwindow.ui
    icon.qrc
    ${CMAKE_CURRENT_BINARY_DIR}/src/collection/license_data.cpp
)

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
    TARGET ${PROJECT_NAME}
    OUTPUT_SCRIPT deploy_script
    CONTENT "
include(\"${QT_DEPLOY_SUPPORT}\")
qt_deploy_runtime_dependencies(
    EXECUTABLE \"${executable_path}\"
    DEPLOY_TOOL_OPTIONS ${deploy_tool_options_arg}
    GENERATE_QT_CONF
    VERBOSE)
")

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${QT_V}::Core ${QT_V}::Gui ${QT_V}::Widgets ${QT_V}::Network ${QT_V}::OpenGLWidgets)


install(TARGETS ${PROJECT_NAME} BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
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
    set(CPACK_NSIS_CREATE_ICONS_EXTRA "CreateShortCut '$DESKTOP\\\\ROCprof Compute Viewer.lnk' '$INSTDIR\\\\bin\\\\rocprof-compute-viewer.exe'")
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
    "${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}"
)

include(CPack)
else()

add_executable(${PROJECT_NAME} ${RCVIEWER_SOURCE_FILES})

if(${QT_VERSION_MAJOR} EQUAL 6)
    set(QT_EXTRA_WIDGET ${QT_V}::OpenGLWidgets)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Threads::Threads ${QT_V}::Network ${QT_V}::Core ${QT_V}::Gui ${QT_V}::Widgets ${QT_EXTRA_WIDGET})

endif()
