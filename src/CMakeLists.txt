file(GLOB_RECURSE GAUGES_SOURCES CONFIGURE_DEPENDS *.cpp ../inc/*.h)

add_executable(gauges ${GAUGES_SOURCES})

target_include_directories(gauges PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/../inc
  ${CMAKE_BINARY_DIR}/generated/include
  ${OpenCV_INCLUDE_DIRS}
)

target_link_libraries(gauges
  PRIVATE
    gauges_options
    gauges_warnings
    ${OpenCV_LIBS}
    Qt6::Widgets
)

target_compile_features(gauges PRIVATE cxx_std_${CMAKE_CXX_STANDARD})

set_target_properties(gauges PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
  AUTOMOC ON
)
