# core must stay free of Windows headers and external dependencies.
add_library(splatthumb_core STATIC
    src/byte_source.cpp
    src/camera.cpp
    src/classify.cpp
    src/file_byte_source.cpp
    src/fingerprint.cpp
    src/loader.cpp
    src/orbit.cpp
    src/ply_header.cpp
    src/render.cpp
    src/settings.cpp
    src/sha256.cpp
    src/splat.cpp
    src/spgm.cpp
    src/status.cpp
    src/version.cpp
    src/view.cpp
    src/view_store.cpp)

target_include_directories(splatthumb_core PUBLIC include)
target_compile_definitions(splatthumb_core PRIVATE
    SPLATTHUMB_VERSION_STRING="${PROJECT_VERSION}")
target_link_libraries(splatthumb_core PRIVATE splatthumb_warnings)
