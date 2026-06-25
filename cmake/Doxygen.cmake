if(${PROJECT_NAME}_ENABLE_DOXYGEN)
  set(DOXYGEN_CALLER_GRAPH YES)
  set(DOXYGEN_CALL_GRAPH YES)
  set(DOXYGEN_EXTRACT_ALL YES)
  set(DOXYGEN_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/docs)
  set(DOXYGEN_ALIASES
    "signal=Signal:"
    "slot=Slot:"
  )

  find_package(Doxygen REQUIRED dot)

  file(GLOB_RECURSE DOXYGEN_SOURCES CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.cpp)
  file(GLOB_RECURSE DOXYGEN_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/inc/*.h)

  doxygen_add_docs(doxygen-docs ${DOXYGEN_SOURCES} ${DOXYGEN_HEADERS})

  message(STATUS "Doxygen has been setup and documentation is now available. Generate using `doxygen-docs` target (ie: cmake --build build --target doxygen-docs)")
endif()
