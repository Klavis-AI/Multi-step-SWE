cmake_minimum_required(VERSION 3.16)
project(mailstack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE MAILSTACK_SRC CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.cpp")
add_executable(mailstack ${MAILSTACK_SRC})
target_include_directories(mailstack PRIVATE "${CMAKE_SOURCE_DIR}/src")
target_link_libraries(mailstack PRIVATE pthread resolv ssl crypto)
target_compile_definitions(mailstack PRIVATE _GNU_SOURCE)

add_custom_command(TARGET mailstack PRE_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_SOURCE_DIR}/sbin")
file(MAKE_DIRECTORY "${CMAKE_SOURCE_DIR}/sbin")
set_target_properties(mailstack PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/sbin")

add_custom_command(TARGET mailstack POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mailstack> "${CMAKE_SOURCE_DIR}/sbin/postfix"
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:mailstack> "${CMAKE_SOURCE_DIR}/sbin/dovecot")
