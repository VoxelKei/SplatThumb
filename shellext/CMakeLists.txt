# Everything but the DLL entry points, so the tests can use the same code without
# registering anything.
add_library(splatthumb_shellext_lib STATIC
    bitmap.cpp
    class_factory.cpp
    embedded_image.cpp
    module.cpp
    stream_byte_source.cpp
    thumbnail_provider.cpp
    user_store.cpp)
target_include_directories(splatthumb_shellext_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(splatthumb_shellext_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(splatthumb_shellext_lib
    PUBLIC splatthumb_core ole32 uuid gdi32 advapi32 shell32 windowscodecs
    PRIVATE splatthumb_warnings)

add_library(splatthumb_shellext SHARED
    dllmain.cpp
    splatthumb_shellext.def)
target_link_libraries(splatthumb_shellext PRIVATE splatthumb_shellext_lib splatthumb_warnings)
