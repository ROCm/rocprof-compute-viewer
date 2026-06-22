find_package(Python3 COMPONENTS Interpreter)

# generate_snapshot_test fails on Windows/macOS/Ubuntu 22, so only enable it on
# Ubuntu 24.
set(RCV_IS_UBUNTU_24 FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND EXISTS "/etc/os-release")
    file(READ "/etc/os-release" _os_release)
    if(_os_release MATCHES "ID=ubuntu" AND _os_release MATCHES "VERSION_ID=\"24")
        set(RCV_IS_UBUNTU_24 TRUE)
    endif()
endif()

if(NOT RCV_IS_UBUNTU_24)
    message(STATUS "Skipping generate_snapshot_test (only enabled on Ubuntu 24)")
elseif(Python3_Interpreter_FOUND)
    add_test(
        NAME generate_snapshot_test
        COMMAND ${Python3_EXECUTABLE} -m unittest discover
                -s ${CMAKE_CURRENT_SOURCE_DIR} -p "test_*.py" -v)
    if(RCV_DECODER_TEST_DATA_DIR)
        set_tests_properties(generate_snapshot_test PROPERTIES
            ENVIRONMENT "RCV_ATT_TEST_ROOT=${RCV_DECODER_TEST_DATA_DIR}")
    endif()
else()
    message(STATUS "Python3 interpreter not found; skipping generate_snapshot_test")
endif()
