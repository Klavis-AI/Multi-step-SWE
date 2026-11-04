add_executable(chargen_client
  chargen_client.cpp
  )

target_link_libraries(chargen_client PRIVATE ise ise_warnings)
