# from here:
#
# https://github.com/lefticus/cppbestpractices/blob/master/02-Use_the_Tools_Available.md

function(
  gauges_set_project_warnings
  project_name
  WARNINGS_AS_ERRORS
  MSVC_WARNINGS
  CLANG_WARNINGS
  GCC_WARNINGS
  CUDA_WARNINGS)
  if("${MSVC_WARNINGS}" STREQUAL "")
    set(MSVC_WARNINGS
        /W4
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /we4289
        /w14296
        /w14311
        /w14545
        /w14546
        /w14547
        /w14549
        /w14555
        /w14619
        /w14640
        /w14826
        /w14905
        /w14906
        /w14928
        /permissive-)
  endif()

  if("${CLANG_WARNINGS}" STREQUAL "")
    set(CLANG_WARNINGS
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wpedantic
        #-Wconversion
        -Wnull-dereference
        #-Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough)
  endif()

  if("${GCC_WARNINGS}" STREQUAL "")
    set(GCC_WARNINGS
        ${CLANG_WARNINGS}
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
        -Wsuggest-override)
  endif()

  if("${CUDA_WARNINGS}" STREQUAL "")
    set(CUDA_WARNINGS
        -Wall
        -Wextra
        -Wunused
        -Wconversion
        -Wshadow)
  endif()

  if(WARNINGS_AS_ERRORS)
    message(TRACE "Warnings are treated as errors")
    list(APPEND CLANG_WARNINGS -Werror)
    list(APPEND GCC_WARNINGS -Werror)
    list(APPEND MSVC_WARNINGS /WX)
  endif()

  if(MSVC)
    set(PROJECT_WARNINGS_CXX ${MSVC_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID MATCHES ".*Clang")
    set(PROJECT_WARNINGS_CXX ${CLANG_WARNINGS})
  elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(PROJECT_WARNINGS_CXX ${GCC_WARNINGS})
  else()
    message(AUTHOR_WARNING "No compiler warnings set for CXX compiler: '${CMAKE_CXX_COMPILER_ID}'")
  endif()

  set(PROJECT_WARNINGS_C "${PROJECT_WARNINGS_CXX}")
  set(PROJECT_WARNINGS_CUDA "${CUDA_WARNINGS}")

  target_compile_options(
    ${project_name}
    INTERFACE
              $<$<COMPILE_LANGUAGE:CXX>:${PROJECT_WARNINGS_CXX}>
              $<$<COMPILE_LANGUAGE:C>:${PROJECT_WARNINGS_C}>
              $<$<COMPILE_LANGUAGE:CUDA>:${PROJECT_WARNINGS_CUDA}>)
endfunction()
