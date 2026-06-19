cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

set(CMAKE_AUTOMOC ON)

project(
  "gauges"
  VERSION 0.1.0
  LANGUAGES CXX
)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
message(STATUS "Started CMake for ${PROJECT_NAME} v${PROJECT_VERSION}...\n")

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.\n")
endif()

add_executable(${PROJECT_NAME}
  src/main.cpp
  src/main_window.h
  src/detection_page.cpp
  src/detection_page.h
  src/calibration_page.cpp
  src/calibration_page.h
  src/processing_page.cpp
  src/processing_page.h
  src/circular_gauge.cpp
  src/circular_gauge.h
  src/worker.cpp
  src/worker.h
  src/q_collapsible_section.cpp
  src/q_collapsible_section.h
  src/gauge_section_helper.h
)

list(APPEND CMAKE_PREFIX_PATH "/home/gustavo/vcpkg/installed/x64-linux")

find_package(OpenCV REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Widgets)

message(STATUS "----------------------- ${OpenCV_INCLUDE_DIRS} ------------------------")

target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCV_INCLUDE_DIRS})

target_link_libraries(${PROJECT_NAME} PRIVATE
  ${OpenCV_LIBS}
  Qt6::Widgets
)
