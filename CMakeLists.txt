cmake_minimum_required(VERSION 3.25)

project(SplatThumb VERSION 0.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Static CRT so users do not need the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(SPLATTHUMB_BUILD_TESTS "Build unit tests" ON)
option(SPLATTHUMB_BUILD_FUZZERS "Build libFuzzer harnesses (clang only)" OFF)
option(SPLATTHUMB_SANITIZE "Build everything with sanitizers (ASan, plus UBSan on clang/gcc)" OFF)

# Must come before any target (including GoogleTest) so the whole program is built the same way.
include(cmake/SplatThumbOptions.cmake)

add_subdirectory(core)
add_subdirectory(cli)

if(WIN32)
    add_subdirectory(shellext)
    add_subdirectory(app)
    add_subdirectory(installer)
endif()

if(SPLATTHUMB_BUILD_FUZZERS)
    if(NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
        message(FATAL_ERROR "SPLATTHUMB_BUILD_FUZZERS requires clang")
    endif()
    add_subdirectory(fuzz)
endif()

if(SPLATTHUMB_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
