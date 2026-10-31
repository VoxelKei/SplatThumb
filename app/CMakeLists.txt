# SplatThumb.exe: the viewpoint window (opened from the PLY right-click menu) and the
# settings window (from the Start menu). Win32 only, static CRT like the DLL, no extra runtime.
add_executable(SplatThumb WIN32
    main.cpp
    ply_search.cpp
    settings_window.cpp
    strings.cpp
    thumbnail_refresh.cpp
    viewer_window.cpp
    SplatThumb.manifest
    SplatThumb.rc)
target_compile_definitions(SplatThumb PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(SplatThumb PRIVATE
    splatthumb_core splatthumb_shellext_lib splatthumb_warnings
    user32 gdi32 shell32 ole32 comctl32)
if(MINGW)
    # wWinMain entry point.
    target_link_options(SplatThumb PRIVATE -municode)
endif()
