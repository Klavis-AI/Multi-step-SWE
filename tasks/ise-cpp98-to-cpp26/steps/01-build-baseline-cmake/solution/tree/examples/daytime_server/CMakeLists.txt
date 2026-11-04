add_executable(daytime_server
  daytime_server.cpp
  )

target_link_libraries(daytime_server PRIVATE ise ise_warnings)
