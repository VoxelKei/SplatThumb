add_library(splatthumb_shellext SHARED
    dllmain.cpp
    splatthumb_shellext.def)

target_compile_definitions(splatthumb_shellext PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(splatthumb_shellext PRIVATE splatthumb_core splatthumb_warnings)
