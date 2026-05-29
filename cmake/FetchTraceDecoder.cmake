# cmake/FetchTraceDecoder.cmake
#
# Shared between the main RCV build and the tests project.
# Resolves the rocprof-trace-decoder in one of three ways and, on success,
# sets RCV_HAS_TRACE_DECODER=ON and makes the namespaced target
# rocprof-trace-decoder::rocprof-trace-decoder-static available.
#
# Cache variables consumed:
#   TRACE_DECODER_ROOT           — existing build/install (mode 1)
#   RCV_FETCH_TRACE_DECODER      — fetch + sub-build (mode 2, default ON)
#   RCV_TRACE_DECODER_REPO       — git URL
#   RCV_TRACE_DECODER_TAG        — branch/tag
#   RCV_TRACE_DECODER_FETCH_DIR  — where to keep the sparse checkout
#                                  (outside the build tree so a clean
#                                  rebuild does not re-clone)
#
# Output:
#   RCV_HAS_TRACE_DECODER (parent scope) — ON if available, else OFF
#   RCV_TRACE_DECODER_SRC_DIR (parent scope, set when mode 2) — path to the
#       checked-out projects/rocprof-trace-decoder directory; useful for
#       reaching the bundled test data.

include_guard(GLOBAL)

option(RCV_FETCH_TRACE_DECODER
    "Fetch and build rocprof-trace-decoder from github.com/ROCm/rocm-systems if TRACE_DECODER_ROOT is not set" OFF)
set(RCV_TRACE_DECODER_REPO "https://github.com/ROCm/rocm-systems.git"
    CACHE STRING "Git repository for the rocprof-trace-decoder source")
set(RCV_TRACE_DECODER_TAG "users/gbaraldi/decoder-build-msvc"
    CACHE STRING "Git branch/tag of rocm-systems to check out for the trace decoder")
# TODO: flip back to "develop" once the decoder fix (LANGUAGES C CXX +
# CMAKE_CXX_STANDARD 20) is merged.
# Keep the sparse checkout outside any single build tree so a clean rebuild
# of the main project or the tests project doesn't trigger a re-clone of the
# (large) rocm-systems monorepo. Resolved relative to the *outermost* source
# dir we can identify so the main project and tests project share one copy.
if(NOT DEFINED RCV_TRACE_DECODER_FETCH_DIR)
    get_filename_component(_rcv_module_root "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
    set(RCV_TRACE_DECODER_FETCH_DIR "${_rcv_module_root}/external/rocm-systems"
        CACHE PATH "Sparse-cloned rocm-systems checkout; lives outside the build tree")
endif()

function(_rcv_trace_decoder_apply_consumer_link_options)
    foreach(_td_target IN ITEMS rocprof-trace-decoder::rocprof-trace-decoder-static rocprof-trace-decoder::rocprof-trace-decoder)
        if(NOT TARGET ${_td_target})
            continue()
        endif()

        set(_td_prefixes "${CMAKE_PREFIX_PATH}")
        if(NOT _td_prefixes AND DEFINED ENV{ROCM_PATH})
            list(APPEND _td_prefixes "$ENV{ROCM_PATH}")
        endif()
        foreach(_td_prefix IN LISTS _td_prefixes)
            set(_td_llvm_lib_dir "${_td_prefix}/lib/llvm/lib")
            set(_td_sysdeps_lib_dir "${_td_prefix}/lib/rocm_sysdeps/lib")
            if(NOT WIN32 AND EXISTS "${_td_llvm_lib_dir}" AND EXISTS "${_td_sysdeps_lib_dir}")
                set_property(TARGET ${_td_target} APPEND PROPERTY INTERFACE_LINK_OPTIONS
                    "-Wl,-rpath-link,${_td_llvm_lib_dir}"
                    "-Wl,-rpath-link,${_td_sysdeps_lib_dir}")
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
        _rcv_trace_decoder_apply_consumer_link_options()
        message(STATUS "Trace-decoder enabled (external): ${rocprof-trace-decoder_DIR}")
        set(RCV_HAS_TRACE_DECODER ON PARENT_SCOPE)
        return()
    endif()

    if(NOT RCV_FETCH_TRACE_DECODER)
        message(STATUS "Trace-decoder disabled (set TRACE_DECODER_ROOT or enable RCV_FETCH_TRACE_DECODER)")
        return()
    endif()

    set(_td_src_dir "${RCV_TRACE_DECODER_FETCH_DIR}")
    set(_td_subdir "projects/rocprof-trace-decoder")
    find_package(Git REQUIRED)

    if(NOT EXISTS "${_td_src_dir}/.git")
        message(STATUS "Fetching rocprof-trace-decoder from ${RCV_TRACE_DECODER_REPO} (${RCV_TRACE_DECODER_TAG}) into ${_td_src_dir}")
        get_filename_component(_td_parent "${_td_src_dir}" DIRECTORY)
        file(MAKE_DIRECTORY "${_td_parent}")
        execute_process(
            COMMAND ${GIT_EXECUTABLE} clone
                --filter=blob:none
                --sparse
                --depth 1
                --branch ${RCV_TRACE_DECODER_TAG}
                ${RCV_TRACE_DECODER_REPO}
                ${_td_src_dir}
            RESULT_VARIABLE _td_clone_result)
        if(NOT _td_clone_result EQUAL 0)
            message(FATAL_ERROR "Failed to clone ${RCV_TRACE_DECODER_REPO} (branch ${RCV_TRACE_DECODER_TAG})")
        endif()
        execute_process(
            COMMAND ${GIT_EXECUTABLE} -C ${_td_src_dir} sparse-checkout set ${_td_subdir}
            RESULT_VARIABLE _td_sparse_result)
        if(NOT _td_sparse_result EQUAL 0)
            message(FATAL_ERROR "Failed to set sparse checkout to ${_td_subdir}")
        endif()
    endif()

    if(NOT EXISTS "${_td_src_dir}/${_td_subdir}/CMakeLists.txt")
        message(FATAL_ERROR
            "Sparse checkout of rocprof-trace-decoder is incomplete at "
            "${_td_src_dir}/${_td_subdir}. Delete ${_td_src_dir} and reconfigure, "
            "or set TRACE_DECODER_ROOT to a pre-built decoder.")
    endif()

    # The decoder's own CMakeLists uses ${CMAKE_SOURCE_DIR} (the top-level
    # source dir) in places where it really means its own source dir, so it
    # cannot safely be add_subdirectory'd. Configure+build it as an isolated
    # sub-build at configure time, then consume via find_package — same path
    # the TRACE_DECODER_ROOT mode uses.
    set(_td_build_dir "${CMAKE_BINARY_DIR}/rocm-systems-build")
    # CI supplies a ROCm SDK with amd_comgr on Windows/Linux. Prefer COMGR
    # there: it avoids global LLVM state collisions and does not depend on
    # LLVM's CMake package/export details. Non-CI builds without COMGR fall
    # back inside the decoder to "no disassembly backend", while parser and
    # ELF metadata support still build.
    set(_td_disasm_args -DUSE_LLVM_DISASM=OFF -DDISABLE_COMGR=OFF)
    set(_td_cfg_args
        -S ${_td_src_dir}/${_td_subdir}
        -B ${_td_build_dir}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        ${_td_disasm_args})
    set(_td_prefix_path "${CMAKE_PREFIX_PATH}")
    if(NOT _td_prefix_path AND DEFINED ENV{ROCM_PATH})
        list(APPEND _td_prefix_path "$ENV{ROCM_PATH}")
    endif()
    if(_td_prefix_path)
        list(APPEND _td_cfg_args "-DCMAKE_PREFIX_PATH=${_td_prefix_path}")
        foreach(_td_prefix IN LISTS _td_prefix_path)
            set(_td_llvm_lib_dir "${_td_prefix}/lib/llvm/lib")
            set(_td_sysdeps_lib_dir "${_td_prefix}/lib/rocm_sysdeps/lib")
            if(NOT WIN32 AND EXISTS "${_td_llvm_lib_dir}" AND EXISTS "${_td_sysdeps_lib_dir}")
                set(_td_rpath_link_flags
                    "-Wl,-rpath-link,${_td_llvm_lib_dir} -Wl,-rpath-link,${_td_sysdeps_lib_dir}")
                list(APPEND _td_cfg_args
                    "-DCMAKE_SHARED_LINKER_FLAGS=${CMAKE_SHARED_LINKER_FLAGS} ${_td_rpath_link_flags}"
                    "-DCMAKE_EXE_LINKER_FLAGS=${CMAKE_EXE_LINKER_FLAGS} ${_td_rpath_link_flags}")
                break()
            endif()
        endforeach()
    endif()
    if(WIN32)
        set(_td_compat_file "${_td_build_dir}/rcv_decoder_compat.cmake")
        file(WRITE "${_td_compat_file}" "if(WIN32 AND NOT TARGET pthread)\n  add_library(pthread INTERFACE IMPORTED)\nendif()\n")
        list(APPEND _td_cfg_args -DCMAKE_PROJECT_INCLUDE=${_td_compat_file})
    endif()
    if(CMAKE_GENERATOR)
        list(APPEND _td_cfg_args -G ${CMAKE_GENERATOR})
    endif()
    if(CMAKE_C_COMPILER)
        list(APPEND _td_cfg_args -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER})
    endif()
    if(CMAKE_CXX_COMPILER)
        list(APPEND _td_cfg_args -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER})
    endif()

    if(NOT EXISTS "${_td_build_dir}/CMakeCache.txt")
        message(STATUS "Configuring rocprof-trace-decoder in ${_td_build_dir}")
        execute_process(
            COMMAND ${CMAKE_COMMAND} ${_td_cfg_args}
            RESULT_VARIABLE _td_cfg_result)
        if(NOT _td_cfg_result EQUAL 0)
            message(FATAL_ERROR "Failed to configure rocprof-trace-decoder (exit ${_td_cfg_result})")
        endif()
    endif()

    message(STATUS "Building rocprof-trace-decoder (first time may take a while)")
    execute_process(
        COMMAND ${CMAKE_COMMAND} --build ${_td_build_dir} --config ${CMAKE_BUILD_TYPE} --parallel
        RESULT_VARIABLE _td_build_result)
    if(NOT _td_build_result EQUAL 0)
        message(FATAL_ERROR "Failed to build rocprof-trace-decoder (exit ${_td_build_result})")
    endif()

    find_package(rocprof-trace-decoder REQUIRED CONFIG
        PATHS ${_td_build_dir}
        PATH_SUFFIXES source
        NO_DEFAULT_PATH)
    _rcv_trace_decoder_apply_consumer_link_options()

    message(STATUS "Trace-decoder enabled (fetched): ${rocprof-trace-decoder_DIR}")
    set(RCV_HAS_TRACE_DECODER ON PARENT_SCOPE)
    set(RCV_TRACE_DECODER_SRC_DIR "${_td_src_dir}/${_td_subdir}" PARENT_SCOPE)
endfunction()


# Extract every *.tar.gz in the decoder's test/data into a flat directory of
# per-dataset folders. Sets RCV_DECODER_TEST_DATA_DIR in parent scope.
function(rcv_extract_decoder_test_data src_dir dest_dir)
    if(NOT EXISTS "${src_dir}/test/data")
        message(WARNING "Decoder test/data dir not found at ${src_dir}/test/data; integration tests will be skipped")
        set(RCV_DECODER_TEST_DATA_DIR "" PARENT_SCOPE)
        return()
    endif()
    file(MAKE_DIRECTORY "${dest_dir}")
    file(GLOB _archives "${src_dir}/test/data/*.tar.gz")
    foreach(_archive IN LISTS _archives)
        get_filename_component(_name "${_archive}" NAME_WE)
        # strip a single trailing .tar if present (NAME_WE only drops .gz)
        string(REGEX REPLACE "\\.tar$" "" _name "${_name}")
        set(_marker "${dest_dir}/.${_name}.extracted")
        if(EXISTS "${_marker}")
            continue()
        endif()
        message(STATUS "Extracting ${_name}.tar.gz")
        execute_process(
            COMMAND ${CMAKE_COMMAND} -E tar xzf "${_archive}"
            WORKING_DIRECTORY "${dest_dir}"
            RESULT_VARIABLE _xr)
        if(NOT _xr EQUAL 0)
            message(WARNING "Failed to extract ${_archive}")
        else()
            file(TOUCH "${_marker}")
        endif()
    endforeach()
    set(RCV_DECODER_TEST_DATA_DIR "${dest_dir}" PARENT_SCOPE)
endfunction()
