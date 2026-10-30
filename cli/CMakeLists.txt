add_executable(splatthumb-cli
    file_byte_source.cpp
    main.cpp)
target_link_libraries(splatthumb-cli PRIVATE splatthumb_core splatthumb_warnings)
