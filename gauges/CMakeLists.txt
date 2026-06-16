cmake_minimum_required(VERSION 3.21)

if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

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

add_executable(${PROJECT_NAME} src/main.cpp src/gauge_detector.cpp src/app_state.h)

list(APPEND CMAKE_PREFIX_PATH "/home/gustavo/vcpkg/installed/x64-linux")

find_package(OpenCV REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)

message(STATUS "----------------------- ${OpenCV_INCLUDE_DIRS} ------------------------")

target_include_directories(${PROJECT_NAME} PRIVATE
                              ${OpenCV_INCLUDE_DIRS}
                           )

target_link_libraries(${PROJECT_NAME} PRIVATE
                        ${OpenCV_LIBS}
                        imgui::imgui
                        glfw
                        OpenGL::GL
                     )
