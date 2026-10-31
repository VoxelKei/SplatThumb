# Third-party PNG encoder, kept out of our warning flags.
add_library(splatthumb_png STATIC png_writer.cpp)
target_include_directories(splatthumb_png SYSTEM PRIVATE ${PROJECT_SOURCE_DIR}/third_party/stb)
target_compile_definitions(splatthumb_png PRIVATE _CRT_SECURE_NO_WARNINGS STBI_MSC_SECURE_CRT)
target_link_libraries(splatthumb_png PUBLIC splatthumb_core)

add_executable(splatthumb-cli
    file_byte_source.cpp
    main.cpp)
target_link_libraries(splatthumb-cli PRIVATE splatthumb_core splatthumb_png splatthumb_warnings)
