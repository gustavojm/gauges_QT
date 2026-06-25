if(${PROJECT_NAME}_ENABLE_DOXYGEN)
  set(DOXYGEN_CALLER_GRAPH YES)
  set(DOXYGEN_CALL_GRAPH YES)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs)

  find_package(Doxygen REQUIRED dot)

  doxygen_add_docs(doxygen-docs ${HEADERS} ${SOURCES})

  message(STATUS "Doxygen has been setup and documentation is now available. Generate using `doxygen-docs` target (ie: cmake --build build --target doxygen-docs)")
endif()
