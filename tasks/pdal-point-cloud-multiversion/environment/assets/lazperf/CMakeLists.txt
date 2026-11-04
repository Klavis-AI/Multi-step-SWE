# Offline vendoring build for laz-perf 3.2.0 (see README.md and PROVENANCE.txt).
#
# This file is written for this image; it is not part of the laz-perf release.
# Upstream's own top-level CMakeLists.txt cannot be used here: it runs
# `file(DOWNLOAD ... autzen.laz)` at configure time while WITH_TESTS is on,
# which cannot succeed with no network. This one builds the library only, from
# the sources in ./lazperf, and defines a single target:
#
#   lazperf    static library, position-independent (alias lazperf::lazperf)
#
# Use it from another build with
#
#   add_subdirectory(<this directory> <binary dir>)
#   target_link_libraries(<your target> PRIVATE lazperf)
#
# and put <this directory> on the include path, so that `#include <lazperf/...>`
# resolves. It also configures standalone:
#
#   cmake -S <this directory> -B <build dir> && cmake --build <build dir>

cmake_minimum_required(VERSION 3.10)

project(lazperf_offline CXX)

file(GLOB LAZPERF_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/lazperf/*.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/lazperf/detail/*.cpp
)

add_library(lazperf STATIC ${LAZPERF_SRCS})
add_library(lazperf::lazperf ALIAS lazperf)
target_include_directories(lazperf PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(lazperf PROPERTIES POSITION_INDEPENDENT_CODE ON)
