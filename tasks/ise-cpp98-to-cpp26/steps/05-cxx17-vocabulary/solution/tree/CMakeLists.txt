cmake_minimum_required(VERSION 3.28)

project(ise
  VERSION 1.0.0
  DESCRIPTION "ise -- a C++ network server framework"
  LANGUAGES CXX)

# The language level is stated once, for the whole project, and is a hard
# requirement rather than a preference: a silent fall-back to whatever the
# compiler happens to default to is how a build stops meaning what it says.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Extensions are off: the one thing the C++98 sources needed them for was
# va_copy in ise_sys_utils.cpp's formatStringV(), which glibc hid behind
# -std=c++98 and which C++11 makes standard. This is the line the step-1 tree
# said would become OFF here.
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

# The project's own warning set, plus -Wextra, promoted to errors. Carried as
# an INTERFACE target so that the library and every example are held to the
# same bar and the flags are written down in exactly one place.
add_library(ise_warnings INTERFACE)
target_compile_options(ise_warnings INTERFACE
  -Wall
  -Wextra
  -Woverloaded-virtual
  -Werror)

find_package(Threads REQUIRED)

add_library(ise STATIC
  ise/main/ise_application.cpp
  ise/main/ise_assert.cpp
  ise/main/ise_classes.cpp
  ise/main/ise_epoll.cpp
  ise/main/ise_event_loop.cpp
  ise/main/ise_exceptions.cpp
  ise/main/ise_http.cpp
  ise/main/ise_inspector.cpp
  ise/main/ise_iocp.cpp
  ise/main/ise_server_assistor.cpp
  ise/main/ise_server_tcp.cpp
  ise/main/ise_server_udp.cpp
  ise/main/ise_socket.cpp
  ise/main/ise_svr_mod.cpp
  ise/main/ise_sys_threads.cpp
  ise/main/ise_sys_utils.cpp
  ise/main/ise_template.cpp
  ise/main/ise_thread.cpp
  ise/main/ise_timer.cpp)

# The include root is the repository root: the project's own headers include
# each other as <ise/main/...>.
target_include_directories(ise PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>)
target_link_libraries(ise
  PRIVATE ise_warnings
  PUBLIC Threads::Threads)

add_subdirectory(examples)

install(TARGETS ise ARCHIVE DESTINATION lib)
install(DIRECTORY ise/main/ DESTINATION include/ise/main FILES_MATCHING PATTERN "*.h")
