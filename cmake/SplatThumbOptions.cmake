# Hardening and sanitizer flags apply to every target, including third-party ones.
# Warnings-as-errors apply only to our own targets, through splatthumb_warnings.

if(MSVC)
    add_compile_options(/utf-8 /permissive- /Zc:__cplusplus /EHsc /sdl)
    add_link_options(/DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA /CETCOMPAT)

    if(SPLATTHUMB_SANITIZE)
        # /RTC and incremental linking are incompatible with ASan.
        string(REGEX REPLACE "/RTC[1csu]+" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
        add_compile_options(/fsanitize=address /Zi)
        add_link_options(/INCREMENTAL:NO /DEBUG)
    else()
        add_compile_options(/guard:cf)
        add_link_options(/guard:cf)
    endif()
else()
    add_compile_options(-fstack-protector-strong)
    if(SPLATTHUMB_SANITIZE)
        add_compile_options(
            -fsanitize=address,undefined
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer)
        add_link_options(-fsanitize=address,undefined)
        add_compile_definitions(_GLIBCXX_ASSERTIONS)
    endif()
endif()

add_library(splatthumb_warnings INTERFACE)
if(MSVC)
    target_compile_options(splatthumb_warnings INTERFACE /W4 /WX)
else()
    target_compile_options(splatthumb_warnings INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Werror)
endif()
