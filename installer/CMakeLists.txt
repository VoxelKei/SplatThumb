# `cmake --build --preset msvc-release --target msi` builds
# <build dir>/msi/<config>/SplatThumb-<version>-x64.msi with the WiX Toolset version pinned
# in .config/dotnet-tools.json. Not part of the default build.
find_program(SPLATTHUMB_DOTNET dotnet)
if(NOT SPLATTHUMB_DOTNET)
    message(STATUS "dotnet not found: the msi target is not available")
    return()
endif()

set(SPLATTHUMB_MSI_NAME "SplatThumb-${PROJECT_VERSION}-x64.msi")
add_custom_target(msi
    COMMAND "${SPLATTHUMB_DOTNET}" tool restore
    COMMAND "${SPLATTHUMB_DOTNET}" tool run wix build
        -arch x64
        -sice ICE61
        -d "Version=${PROJECT_VERSION}"
        -d "ShellExtDll=$<TARGET_FILE:splatthumb_shellext>"
        -d "LicenseFile=${PROJECT_SOURCE_DIR}/LICENSE"
        -o "${CMAKE_BINARY_DIR}/msi/$<CONFIG>/${SPLATTHUMB_MSI_NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Package.wxs"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building ${SPLATTHUMB_MSI_NAME}"
    VERBATIM)
add_dependencies(msi splatthumb_shellext)
