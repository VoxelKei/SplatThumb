include(FetchContent)

FetchContent_Declare(googletest
    URL https://github.com/google/googletest/releases/download/v1.17.0/googletest-1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(splatthumb_tests
    version_test.cpp)
target_link_libraries(splatthumb_tests PRIVATE splatthumb_core splatthumb_warnings GTest::gtest_main)

include(GoogleTest)
# PRE_TEST: discovery runs at test time, when sanitizer runtimes are on PATH.
gtest_discover_tests(splatthumb_tests DISCOVERY_MODE PRE_TEST)
