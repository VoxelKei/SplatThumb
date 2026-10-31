# core must stay free of Windows headers and external dependencies.
add_library(splatthumb_core STATIC
    src/byte_source.cpp
    src/camera.cpp
    src/classify.cpp
    src/loader.cpp
    src/ply_header.cpp
    src/splat.cpp
    src/status.cpp
    src/version.cpp)

target_include_directories(splatthumb_core PUBLIC include)
target_compile_definitions(splatthumb_core PRIVATE
    SPLATTHUMB_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(splatthumb_core PRIVATE splatthumb_warnings)
