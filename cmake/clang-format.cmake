#
# Add a target for formatting the project using `clang-format` (i.e: cmake --build build --target clang-format)
#

function(add_clang_format_target)
  if(NOT ${PROJECT_NAME}_CLANG_FORMAT_BINARY)
    find_program(${PROJECT_NAME}_CLANG_FORMAT_BINARY clang-format)
  endif()

  if(${PROJECT_NAME}_CLANG_FORMAT_BINARY)
    add_custom_target(
      clang-format
      COMMAND ${${PROJECT_NAME}_CLANG_FORMAT_BINARY} -i --verbose ${SOURCES} ${HEADERS}
      WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
      COMMENT "Formatting with ${${PROJECT_NAME}_CLANG_FORMAT_BINARY}...")

    message(STATUS "Format the project using the `clang-format` target (i.e: cmake --build build --target clang-format).\n")
  endif()
endfunction()

add_clang_format_target()
