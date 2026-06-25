include(cmake/LibFuzzer.cmake)
include(CMakeDependentOption)
include(CheckCXXCompilerFlag)
include(CheckCXXSourceCompiles)

macro(gauges_supports_sanitizers)
  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND NOT WIN32)
    message(STATUS "Sanity checking UndefinedBehaviorSanitizer, it should be supported on this platform")
    set(TEST_PROGRAM "int main() { return 0; }")

    set(CMAKE_REQUIRED_FLAGS "-fsanitize=undefined")
    set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=undefined")
    check_cxx_source_compiles("${TEST_PROGRAM}" HAS_UBSAN_LINK_SUPPORT)

    if(HAS_UBSAN_LINK_SUPPORT)
      message(STATUS "UndefinedBehaviorSanitizer is supported at both compile and link time.")
      set(SUPPORTS_UBSAN ON)
    else()
      message(WARNING "UndefinedBehaviorSanitizer is NOT supported at link time.")
      set(SUPPORTS_UBSAN OFF)
    endif()
  else()
    set(SUPPORTS_UBSAN OFF)
  endif()

  if((CMAKE_CXX_COMPILER_ID MATCHES ".*Clang.*" OR CMAKE_CXX_COMPILER_ID MATCHES ".*GNU.*") AND WIN32)
    set(SUPPORTS_ASAN OFF)
  else()
    if (NOT WIN32)
      message(STATUS "Sanity checking AddressSanitizer, it should be supported on this platform")
      set(TEST_PROGRAM "int main() { return 0; }")

      set(CMAKE_REQUIRED_FLAGS "-fsanitize=address")
      set(CMAKE_REQUIRED_LINK_OPTIONS "-fsanitize=address")
      check_cxx_source_compiles("${TEST_PROGRAM}" HAS_ASAN_LINK_SUPPORT)

      if(HAS_ASAN_LINK_SUPPORT)
        message(STATUS "AddressSanitizer is supported at both compile and link time.")
        set(SUPPORTS_ASAN ON)
      else()
        message(WARNING "AddressSanitizer is NOT supported at link time.")
        set(SUPPORTS_ASAN OFF)
      endif()
    else()
      set(SUPPORTS_ASAN ON)
    endif()
  endif()
endmacro()

macro(gauges_setup_options)
  option(gauges_ENABLE_HARDENING "Enable hardening" ON)
  option(gauges_ENABLE_COVERAGE "Enable coverage reporting" OFF)
  option(gauges_ENABLE_DOXYGEN "Enable Doxygen documentation" OFF)
  option(gauges_CLANG_FORMAT_BINARY "Path to clang-format binary" "")
  cmake_dependent_option(
    gauges_ENABLE_GLOBAL_HARDENING
    "Attempt to push hardening options to built dependencies"
    ON
    gauges_ENABLE_HARDENING
    OFF)

  gauges_supports_sanitizers()

  if(NOT PROJECT_IS_TOP_LEVEL OR gauges_PACKAGING_MAINTAINER_MODE)
    option(gauges_ENABLE_IPO "Enable IPO/LTO" OFF)
    option(gauges_WARNINGS_AS_ERRORS "Treat Warnings As Errors" OFF)
    option(gauges_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(gauges_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(gauges_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(gauges_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(gauges_ENABLE_PCH "Enable precompiled headers" OFF)
    option(gauges_ENABLE_CACHE "Enable ccache" OFF)
  else()
    option(gauges_ENABLE_IPO "Enable IPO/LTO" ON)
    option(gauges_WARNINGS_AS_ERRORS "Treat Warnings As Errors" ON)
    option(gauges_ENABLE_SANITIZER_ADDRESS "Enable address sanitizer" ${SUPPORTS_ASAN})
    option(gauges_ENABLE_SANITIZER_LEAK "Enable leak sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_UNDEFINED "Enable undefined sanitizer" ${SUPPORTS_UBSAN})
    option(gauges_ENABLE_SANITIZER_THREAD "Enable thread sanitizer" OFF)
    option(gauges_ENABLE_SANITIZER_MEMORY "Enable memory sanitizer" OFF)
    option(gauges_ENABLE_UNITY_BUILD "Enable unity builds" OFF)
    option(gauges_ENABLE_CLANG_TIDY "Enable clang-tidy" OFF)
    option(gauges_ENABLE_CPPCHECK "Enable cpp-check analysis" OFF)
    option(gauges_ENABLE_PCH "Enable precompiled headers" OFF)
    option(gauges_ENABLE_CACHE "Enable ccache" ON)
  endif()

  if(NOT PROJECT_IS_TOP_LEVEL)
    mark_as_advanced(
      gauges_ENABLE_IPO
      gauges_WARNINGS_AS_ERRORS
      gauges_ENABLE_SANITIZER_ADDRESS
      gauges_ENABLE_SANITIZER_LEAK
      gauges_ENABLE_SANITIZER_UNDEFINED
      gauges_ENABLE_SANITIZER_THREAD
      gauges_ENABLE_SANITIZER_MEMORY
      gauges_ENABLE_UNITY_BUILD
      gauges_ENABLE_CLANG_TIDY
      gauges_ENABLE_CPPCHECK
      gauges_ENABLE_COVERAGE
      gauges_ENABLE_PCH
      gauges_ENABLE_CACHE)
  endif()

  gauges_check_libfuzzer_support(LIBFUZZER_SUPPORTED)
  if(LIBFUZZER_SUPPORTED AND (gauges_ENABLE_SANITIZER_ADDRESS OR gauges_ENABLE_SANITIZER_THREAD OR gauges_ENABLE_SANITIZER_UNDEFINED))
    set(DEFAULT_FUZZER ON)
  else()
    set(DEFAULT_FUZZER OFF)
  endif()

  option(gauges_BUILD_FUZZ_TESTS "Enable fuzz testing executable" ${DEFAULT_FUZZER})

endmacro()

macro(gauges_global_options)
  if(gauges_ENABLE_IPO)
    include(cmake/InterproceduralOptimization.cmake)
    gauges_enable_ipo()
  endif()

  gauges_supports_sanitizers()

  if(gauges_ENABLE_HARDENING AND gauges_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN
       OR gauges_ENABLE_SANITIZER_UNDEFINED
       OR gauges_ENABLE_SANITIZER_ADDRESS
       OR gauges_ENABLE_SANITIZER_THREAD
       OR gauges_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    gauges_enable_hardening(gauges_options ON ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()
endmacro()

macro(gauges_local_options)
  if(PROJECT_IS_TOP_LEVEL)
    include(cmake/StandardProjectSettings.cmake)
  endif()

  add_library(gauges_warnings INTERFACE)
  add_library(gauges_options INTERFACE)

  include(cmake/CompilerWarnings.cmake)
  gauges_set_project_warnings(
    gauges_warnings
    ${gauges_WARNINGS_AS_ERRORS}
    ""
    ""
    ""
    "")

  include(cmake/Linker.cmake)

  if(NOT EMSCRIPTEN)
    include(cmake/Sanitizers.cmake)
    gauges_enable_sanitizers(
      gauges_options
      ${gauges_ENABLE_SANITIZER_ADDRESS}
      ${gauges_ENABLE_SANITIZER_LEAK}
      ${gauges_ENABLE_SANITIZER_UNDEFINED}
      ${gauges_ENABLE_SANITIZER_THREAD}
      ${gauges_ENABLE_SANITIZER_MEMORY})
  endif()

  set_target_properties(gauges_options PROPERTIES UNITY_BUILD ${gauges_ENABLE_UNITY_BUILD})

  if(gauges_ENABLE_PCH)
    target_precompile_headers(
      gauges_options
      INTERFACE
      <vector>
      <string>
      <utility>)
  endif()

  if(gauges_ENABLE_CACHE)
    include(cmake/Cache.cmake)
    gauges_enable_cache()
  endif()

  include(cmake/StaticAnalyzers.cmake)
  if(gauges_ENABLE_CLANG_TIDY)
    gauges_enable_clang_tidy(gauges_options ${gauges_WARNINGS_AS_ERRORS})
  endif()

  if(gauges_ENABLE_CPPCHECK)
    gauges_enable_cppcheck(${gauges_WARNINGS_AS_ERRORS} "")
  endif()

  if(gauges_ENABLE_COVERAGE)
    include(cmake/Tests.cmake)
    gauges_enable_coverage(gauges_options)
  endif()

  if(gauges_WARNINGS_AS_ERRORS)
    check_cxx_compiler_flag("-Wl,--fatal-warnings" LINKER_FATAL_WARNINGS)
    if(LINKER_FATAL_WARNINGS)
      # This is not working consistently, so disabling for now
      # target_link_options(gauges_options INTERFACE -Wl,--fatal-warnings)
    endif()
  endif()

  if(gauges_ENABLE_HARDENING AND NOT gauges_ENABLE_GLOBAL_HARDENING)
    include(cmake/Hardening.cmake)
    if(NOT SUPPORTS_UBSAN
       OR gauges_ENABLE_SANITIZER_UNDEFINED
       OR gauges_ENABLE_SANITIZER_ADDRESS
       OR gauges_ENABLE_SANITIZER_THREAD
       OR gauges_ENABLE_SANITIZER_LEAK)
      set(ENABLE_UBSAN_MINIMAL_RUNTIME FALSE)
    else()
      set(ENABLE_UBSAN_MINIMAL_RUNTIME TRUE)
    endif()
    gauges_enable_hardening(gauges_options OFF ${ENABLE_UBSAN_MINIMAL_RUNTIME})
  endif()

  if(gauges_ENABLE_DOXYGEN)
    include(cmake/Doxygen.cmake)
  endif()

  include(cmake/clang-format.cmake)

endmacro()
