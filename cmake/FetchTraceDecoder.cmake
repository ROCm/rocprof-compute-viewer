# Shared by the app and tests. Provides:
#   rcv_fetch_trace_decoder()
#   rcv_extract_decoder_test_data(src_dir dest_dir)

include_guard(GLOBAL)

option(RCV_FETCH_TRACE_DECODER "Fetch and build rocprof-trace-decoder when TRACE_DECODER_ROOT is not set" OFF)
set(RCV_TRACE_DECODER_REPO "https://github.com/ROCm/rocm-systems.git"
    CACHE STRING "Git repository for rocprof-trace-decoder")
set(RCV_TRACE_DECODER_TAG "users/gbaraldi/decoder-build-msvc"
    CACHE STRING "Git branch/tag for rocprof-trace-decoder")
if(NOT DEFINED RCV_TRACE_DECODER_FETCH_DIR)
    get_filename_component(_rcv_repo_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(RCV_TRACE_DECODER_FETCH_DIR "${_rcv_repo_root}/external/rocm-systems"
        CACHE PATH "Sparse checkout for rocm-systems")
endif()

function(_rcv_rocm_prefixes out_var)
    set(_prefixes ${CMAKE_PREFIX_PATH})
    if(NOT _prefixes AND DEFINED ENV{ROCM_PATH})
        list(APPEND _prefixes "$ENV{ROCM_PATH}")
    endif()
    set(${out_var} ${_prefixes} PARENT_SCOPE)
endfunction()

function(_rcv_add_rocm_rpath_links)
    if(WIN32)
        return()
    endif()

    _rcv_rocm_prefixes(_prefixes)
    foreach(_target IN ITEMS rocprof-trace-decoder::rocprof-trace-decoder-static rocprof-trace-decoder::rocprof-trace-decoder)
        if(NOT TARGET ${_target})
            continue()
        endif()
        foreach(_prefix IN LISTS _prefixes)
            set(_llvm_lib "${_prefix}/lib/llvm/lib")
            set(_sysdeps_lib "${_prefix}/lib/rocm_sysdeps/lib")
            if(EXISTS "${_llvm_lib}" AND EXISTS "${_sysdeps_lib}")
                set_property(TARGET ${_target} APPEND PROPERTY INTERFACE_LINK_OPTIONS
                    "-Wl,-rpath-link,${_llvm_lib}"
                    "-Wl,-rpath-link,${_sysdeps_lib}")
            endif()
        endforeach()
    endforeach()
endfunction()

function(rcv_fetch_trace_decoder)
    set(RCV_HAS_TRACE_DECODER OFF PARENT_SCOPE)

    if(DEFINED TRACE_DECODER_ROOT)
        find_package(rocprof-trace-decoder REQUIRED CONFIG
            PATHS ${TRACE_DECODER_ROOT}
            PATH_SUFFIXES source lib/cmake/rocprof-trace-decoder
            NO_DEFAULT_PATH)
        _rcv_add_rocm_rpath_links()
        message(STATUS "Trace-decoder enabled: ${rocprof-trace-decoder_DIR}")
        set(RCV_HAS_TRACE_DECODER ON PARENT_SCOPE)
        return()
    endif()

    if(NOT RCV_FETCH_TRACE_DECODER)
        message(STATUS "Trace-decoder disabled")
        return()
    endif()

    set(_src_dir "${RCV_TRACE_DECODER_FETCH_DIR}")
    set(_subdir "projects/rocprof-trace-decoder")
    set(_decoder_src "${_src_dir}/${_subdir}")
    set(_build_dir "${CMAKE_BINARY_DIR}/rocm-systems-build")

    find_package(Git REQUIRED)
    if(NOT EXISTS "${_src_dir}/.git")
        message(STATUS "Fetching rocprof-trace-decoder from ${RCV_TRACE_DECODER_REPO} (${RCV_TRACE_DECODER_TAG})")
        get_filename_component(_src_parent "${_src_dir}" DIRECTORY)
        file(MAKE_DIRECTORY "${_src_parent}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone --filter=blob:none --sparse --depth 1
                    --branch ${RCV_TRACE_DECODER_TAG} ${RCV_TRACE_DECODER_REPO} ${_src_dir}
            RESULT_VARIABLE _clone_result)
        if(NOT _clone_result EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${RCV_TRACE_DECODER_REPO}")
        endif()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${_src_dir} sparse-checkout set ${_subdir}
            RESULT_VARIABLE _sparse_result)
        if(NOT _sparse_result EQUAL 0)
            message(FATAL_ERROR "Failed to sparse-checkout ${_subdir}")
        endif()
    endif()
    if(NOT EXISTS "${_decoder_src}/CMakeLists.txt")
        message(FATAL_ERROR "rocprof-trace-decoder checkout is incomplete at ${_decoder_src}")
    endif()

    set(_cfg_args
        -S ${_decoder_src}
        -B ${_build_dir}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        -DUSE_LLVM_DISASM=OFF
        -DDISABLE_COMGR=OFF)

    _rcv_rocm_prefixes(_prefixes)
    if(_prefixes)
        list(APPEND _cfg_args "-DCMAKE_PREFIX_PATH=${_prefixes}")
    endif()
    foreach(_prefix IN LISTS _prefixes)
        if(NOT WIN32 AND EXISTS "${_prefix}/lib/llvm/lib" AND EXISTS "${_prefix}/lib/rocm_sysdeps/lib")
            set(_rpath_link "-Wl,-rpath-link,${_prefix}/lib/llvm/lib -Wl,-rpath-link,${_prefix}/lib/rocm_sysdeps/lib")
            list(APPEND _cfg_args
                "-DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS} ${_rpath_link}"
                "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} ${_rpath_link}")
            break()
        endif()
    endforeach()
    if(WIN32)
        set(_compat_file "${_build_dir}/rcv_decoder_compat.cmake")
        file(WRITE "${_compat_file}" "if(WIN32 AND NOT TARGET pthread)\n  add_library(pthread INTERFACE IMPORTED)\nendif()\n")
        list(APPEND _cfg_args -DCMAKE_PROJECT_INCLUDE=${_compat_file})
    endif()
    if(CMAKE_GENERATOR)
        list(APPEND _cfg_args -G "${CMAKE_GENERATOR}")
    endif()
    foreach(_arg IN ITEMS CMAKE_C_COMPILER CMAKE_CXX_COMPILER)
        if(DEFINED ${_arg} AND NOT "${${_arg}}" STREQUAL "")
            list(APPEND _cfg_args "-D${_arg}=${${_arg}}")
        endif()
    endforeach()

    execute_process(COMMAND ${CMAKE_COMMAND} ${_cfg_args} RESULT_VARIABLE _cfg_result)
    if(NOT _cfg_result EQUAL 0)
        message(FATAL_ERROR "Failed to configure rocprof-trace-decoder")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${_build_dir} --config ${CMAKE_BUILD_TYPE} --parallel
        RESULT_VARIABLE _build_result)
    if(NOT _build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build rocprof-trace-decoder")
    endif()

    find_package(rocprof-trace-decoder REQUIRED CONFIG
        PATHS ${_build_dir}
        PATH_SUFFIXES source
        NO_DEFAULT_PATH)
    _rcv_add_rocm_rpath_links()
    message(STATUS "Trace-decoder enabled: ${rocprof-trace-decoder_DIR}")
    set(RCV_HAS_TRACE_DECODER ON PARENT_SCOPE)
    set(RCV_TRACE_DECODER_SRC_DIR "${_decoder_src}" PARENT_SCOPE)
endfunction()

function(rcv_extract_decoder_test_data src_dir dest_dir)
    if(NOT EXISTS "${src_dir}/test/data")
        set(RCV_DECODER_TEST_DATA_DIR "" PARENT_SCOPE)
        return()
    endif()

    file(MAKE_DIRECTORY "${dest_dir}")
    file(GLOB _archives "${src_dir}/test/data/*.tar.gz")
    foreach(_archive IN LISTS _archives)
        get_filename_component(_name "${_archive}" NAME_WE)
        string(REGEX REPLACE "\\.tar$" "" _name "${_name}")
        set(_marker "${dest_dir}/.${_name}.extracted")
        if(EXISTS "${_marker}")
            continue()
        endif()
        execute_process(COMMAND ${CMAKE_COMMAND} -E tar xzf "${_archive}" WORKING_DIRECTORY "${dest_dir}")
        file(TOUCH "${_marker}")
    endforeach()
    set(RCV_DECODER_TEST_DATA_DIR "${dest_dir}" PARENT_SCOPE)
endfunction()
