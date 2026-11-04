add_executable(udp_client
  udp_client.cpp
  )

target_link_libraries(udp_client PRIVATE ise ise_warnings)
