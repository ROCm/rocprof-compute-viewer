# Injected into the rocprof-trace-decoder sub-build via
# CMAKE_PROJECT_TOP_LEVEL_INCLUDES (set in FetchTraceDecoder.cmake).
#
# The decoder declares project(... LANGUAGES CXX), but LLVM 18's installed
# LLVMConfig.cmake pulls in FindFFI.cmake / FindTerminfo.cmake which call
# check_c_source_compiles — that requires the C language to be enabled.
# Enable it here so the find_package(LLVM) call inside the decoder succeeds.
enable_language(C)
