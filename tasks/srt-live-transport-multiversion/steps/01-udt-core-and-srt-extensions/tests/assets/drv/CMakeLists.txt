# Verifier-owned driver project. Configured and built at /tmp, never inside
# /app, and never added to the candidate's own CMakeLists.txt: a candidate must
# not be able to edit its own acceptance.
#
# One executable per group family. A compile error in one family must not be
# reported as a capability gap in the others, so test.sh checks for each
# binary separately and attributes a missing one as `driver-did-not-compile`.
cmake_minimum_required(VERSION 3.10)
project(srtcheck CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_BUILD_TYPE Release)

# CANDIDATE_PREFIX is the DESTDIR staging tree the candidate's own
# `make install` produced. Linking against that, rather than against build
# artefacts found by globbing, is what makes the documented install layout the
# real contract.
if(NOT DEFINED CANDIDATE_PREFIX)
  message(FATAL_ERROR "CANDIDATE_PREFIX must be set to the candidate's install staging prefix")
endif()

find_library(CANDIDATE_SRT NAMES srt PATHS "${CANDIDATE_PREFIX}/lib" NO_DEFAULT_PATH)
if(NOT CANDIDATE_SRT)
  message(FATAL_ERROR "no libsrt under ${CANDIDATE_PREFIX}/lib")
endif()
if(NOT EXISTS "${CANDIDATE_PREFIX}/include/srt/srt.h")
  message(FATAL_ERROR "no srt/srt.h under ${CANDIDATE_PREFIX}/include")
endif()

find_package(Threads REQUIRED)

# DRIVER_DEFS carries the preprocessor flags the candidate's library was
# configured with that also change the INSTALLED header. `SRTO_CRYPTOMODE` and
# `SRTO_MAXREXMITBW` are each inside an `#ifdef` in the shipped `srt/srt.h`, and
# the flag that enables them reaches the library through `add_definitions` --
# which never lands in `srt.pc` or in any generated header. A driver compiled
# without the same `-D` cannot see the option it is there to assert, and would
# fail a correct implementation. test.sh sets it alongside the step's cmake
# recipe, so the candidate's configure line and the driver's compile line agree.
#
# add_compile_definitions() prepends its own -D and takes BARE macro names, so
# handing it "-DFOO=1" produces "-D-DFOO=1": gcc rejects that with
# "macro names must be identifiers" AND leaves FOO undefined, so every driver
# that needs the symbol fails to compile for a reason that looks nothing like
# the real cause. test.sh passes DRIVER_DEFS in compiler form (`-DFOO`), so the
# -D is stripped here.
if(DEFINED DRIVER_DEFS AND NOT DRIVER_DEFS STREQUAL "")
  separate_arguments(DRIVER_DEFS_LIST UNIX_COMMAND "${DRIVER_DEFS}")
  list(TRANSFORM DRIVER_DEFS_LIST REPLACE "^-D" "")
  add_compile_definitions(${DRIVER_DEFS_LIST})
endif()

file(GLOB DRIVER_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/fam_*.cpp")
if(NOT DRIVER_SOURCES)
  message(FATAL_ERROR "no fam_*.cpp driver families found")
endif()

foreach(src ${DRIVER_SOURCES})
  get_filename_component(fam "${src}" NAME_WE)
  add_executable(${fam} "${src}")
  target_include_directories(${fam} PRIVATE
      "${CANDIDATE_PREFIX}/include" "${CMAKE_CURRENT_SOURCE_DIR}")
  # ${CMAKE_DL_LIBS} is `dl` on this base image (glibc 2.31 still ships a
  # separate libdl). A group that has to prove a symbol is *exported* from the
  # candidate's shared library -- rather than merely declared in its header --
  # resolves it with dlsym instead of calling it, so that a candidate which
  # never declared it loses that one group instead of failing to compile the
  # whole family. Without this, such a driver does not link at all.
  target_link_libraries(${fam} PRIVATE "${CANDIDATE_SRT}" Threads::Threads ${CMAKE_DL_LIBS})
  # The candidate's shared library lives in the staging tree, not on the
  # loader's default path.
  set_target_properties(${fam} PROPERTIES
      BUILD_RPATH "${CANDIDATE_PREFIX}/lib"
      INSTALL_RPATH "${CANDIDATE_PREFIX}/lib")
endforeach()
