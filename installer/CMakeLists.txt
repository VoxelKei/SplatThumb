# Custom actions: the .ply part of the registration, which the MSI's Registry table
# cannot undo exactly (see customaction/ply_registration.h).
add_library(splatthumb_ca_lib STATIC customaction/ply_registration.cpp)
target_include_directories(splatthumb_ca_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/customaction)
target_compile_definitions(splatthumb_ca_lib PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
target_link_libraries(splatthumb_ca_lib PUBLIC advapi32 PRIVATE splatthumb_warnings)

add_library(splatthumb_ca SHARED
    customaction/custom_actions.cpp
    customaction/splatthumb_ca.def)
target_link_libraries(splatthumb_ca PRIVATE splatthumb_ca_lib msi shell32 splatthumb_warnings)

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
        -d "Version=${PROJECT_VERSION}"
        -d "ShellExtDll=$<TARGET_FILE:splatthumb_shellext>"
        -d "CustomActionDll=$<TARGET_FILE:splatthumb_ca>"
        -d "LicenseFile=${PROJECT_SOURCE_DIR}/LICENSE"
        -o "${CMAKE_BINARY_DIR}/msi/$<CONFIG>/${SPLATTHUMB_MSI_NAME}"
        "${CMAKE_CURRENT_SOURCE_DIR}/Package.wxs"
    # Windows Installer's own consistency checks (ICEs). ICE61 only warns that
    # AllowSameVersionUpgrades is on, which is intended.
    COMMAND "${SPLATTHUMB_DOTNET}" tool run wix msi validate -sice ICE61
        "${CMAKE_BINARY_DIR}/msi/$<CONFIG>/${SPLATTHUMB_MSI_NAME}"
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMENT "Building ${SPLATTHUMB_MSI_NAME}"
    VERBATIM)
add_dependencies(msi splatthumb_shellext splatthumb_ca)
