cmake_minimum_required(VERSION 3.21)

# Only set the cxx_standard if it is not set by someone else
if (NOT DEFINED CMAKE_CXX_STANDARD)
  set(CMAKE_CXX_STANDARD 17)
endif()

# strongly encouraged to enable this globally to avoid conflicts between
# -Wpedantic being enabled and -std=c++20 and -std=gnu++20 for example
# when compiling with PCH enabled
set(CMAKE_CXX_EXTENSIONS OFF)

# Set the project name and language
project(
  gauges
  VERSION 0.1.0
  DESCRIPTION "Qt6 + OpenCV gauge reader"
  LANGUAGES CXX)

# Enable Qt MOC (must be after project())
set(CMAKE_AUTOMOC ON)

include(cmake/PreventInSourceBuilds.cmake)
include(ProjectOptions.cmake)

gauges_setup_options()

gauges_global_options()
include(Dependencies.cmake)
gauges_setup_dependencies()

gauges_local_options()

set(CMAKE_CXX_VISIBILITY_PRESET hidden)

set(GIT_SHA
    "Unknown"
    CACHE STRING "SHA this build was generated from")
string(
  SUBSTRING "${GIT_SHA}"
            0
            8
            GIT_SHORT_SHA)

target_compile_features(gauges_options INTERFACE cxx_std_${CMAKE_CXX_STANDARD})

string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPERCASE)
configure_file(cmake/version.hpp.in ${CMAKE_BINARY_DIR}/generated/include/version.hpp)

add_library(gauges::gauges_options ALIAS gauges_options)
add_library(gauges::gauges_warnings ALIAS gauges_warnings)

# Adding the src:
add_subdirectory(src)

# Don't even look at tests if we're not top level
if(NOT PROJECT_IS_TOP_LEVEL)
  return()
endif()

# Adding the tests:
include(CTest)
if(BUILD_TESTING)
  message(AUTHOR_WARNING "Building Tests. Be sure to check out test/constexpr_tests.cpp for constexpr testing")
  # add_subdirectory(test)
endif()

if(CMAKE_SKIP_INSTALL_RULES)
  return()
endif()

# include(cmake/PackageProject.cmake)

# gauges_package_project(
#   TARGETS
#   gauges
#   gauges_options
#   gauges_warnings
# )

# # Experience shows that explicit package naming can help make it easier to sort
# # out potential ABI related issues before they start, while helping you
# # track a build to a specific GIT SHA
# set(CPACK_PACKAGE_FILE_NAME
#     "${CMAKE_PROJECT_NAME}-${CMAKE_PROJECT_VERSION}-${GIT_SHORT_SHA}-${CMAKE_SYSTEM_NAME}-${CMAKE_BUILD_TYPE}-${CMAKE_CXX_COMPILER_ID}-${CMAKE_CXX_COMPILER_VERSION}")

# include(CPack)
