include(cmake/CPM.cmake)

# Done as a function so that updates to variables like
# CMAKE_CXX_FLAGS don't propagate out to other
# targets
function(gauges_setup_dependencies)

  # For each dependency, see if it's
  # already been provided to us by a parent project

  find_package(OpenCV REQUIRED)
  find_package(Qt6 REQUIRED COMPONENTS Widgets)

  # Propagate OpenCV variables to parent scope
  set(OpenCV_INCLUDE_DIRS ${OpenCV_INCLUDE_DIRS} PARENT_SCOPE)
  set(OpenCV_LIBS ${OpenCV_LIBS} PARENT_SCOPE)

endfunction()
