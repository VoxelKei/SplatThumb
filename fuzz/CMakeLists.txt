add_executable(fuzz_header fuzz_header.cpp)
target_link_libraries(fuzz_header PRIVATE splatthumb_core splatthumb_warnings)
target_link_options(fuzz_header PRIVATE -fsanitize=fuzzer)

add_executable(fuzz_splats fuzz_splats.cpp)
target_link_libraries(fuzz_splats PRIVATE splatthumb_core splatthumb_warnings)
target_link_options(fuzz_splats PRIVATE -fsanitize=fuzzer)
