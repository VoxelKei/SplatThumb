# M1 spike: macOS host app + Quick Look thumbnail extension.
# Separate project because it needs the Xcode generator (app extensions and signing);
# the core library is used straight from ../core, not copied.
#
#   cmake -S macos -B build/macos-xcode -G Xcode -DSPLATTHUMB_TEAM_ID=XXXXXXXXXX
#   cmake --build build/macos-xcode --config Release
#
# Without SPLATTHUMB_TEAM_ID everything is signed ad hoc ("Sign to Run Locally").
cmake_minimum_required(VERSION 3.25)

set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "")
set(CMAKE_OSX_ARCHITECTURES "arm64" CACHE STRING "")

project(SplatThumbMac VERSION 0.4.0 LANGUAGES C CXX OBJC OBJCXX)

if(NOT CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "macos/ must be configured with -G Xcode")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_OBJCXX_STANDARD 20)
set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
set(CMAKE_OBJCXX_EXTENSIONS OFF)

set(SPLATTHUMB_TEAM_ID "" CACHE STRING "Apple developer team ID (empty = ad hoc signing)")

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/SplatThumbOptions.cmake)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/core)

function(splatthumb_sign target entitlements)
    set_target_properties(${target} PROPERTIES
        XCODE_ATTRIBUTE_CODE_SIGN_ENTITLEMENTS "${entitlements}"
        XCODE_ATTRIBUTE_ENABLE_HARDENED_RUNTIME YES
        XCODE_ATTRIBUTE_CODE_SIGN_STYLE Manual
        XCODE_ATTRIBUTE_CODE_SIGN_INJECT_BASE_ENTITLEMENTS NO)
    if(SPLATTHUMB_TEAM_ID)
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_DEVELOPMENT_TEAM "${SPLATTHUMB_TEAM_ID}"
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "Apple Development")
    else()
        set_target_properties(${target} PROPERTIES
            XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")
    endif()
endfunction()

# --- Quick Look thumbnail extension -----------------------------------------
add_executable(SplatThumbThumbnail MACOSX_BUNDLE thumbnail/ThumbnailProvider.mm)
target_link_libraries(SplatThumbThumbnail PRIVATE
    splatthumb_core
    "-framework Foundation"
    "-framework CoreGraphics"
    "-framework ImageIO"
    "-framework QuickLookThumbnailing")
set_target_properties(SplatThumbThumbnail PROPERTIES
    BUNDLE_EXTENSION appex
    XCODE_PRODUCT_TYPE com.apple.product-type.app-extension
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/thumbnail/Info.plist
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.voxelkei.splatthumb.thumbnail
    XCODE_ATTRIBUTE_APPLICATION_EXTENSION_API_ONLY YES
    XCODE_ATTRIBUTE_OTHER_LDFLAGS "-e _NSExtensionMain"
    XCODE_ATTRIBUTE_SKIP_INSTALL YES)
splatthumb_sign(SplatThumbThumbnail ${CMAKE_CURRENT_SOURCE_DIR}/thumbnail/SplatThumbThumbnail.entitlements)

# --- Host app ----------------------------------------------------------------
add_executable(SplatThumb MACOSX_BUNDLE app/main.m app/STViewerWindow.mm icon/STIconSetter.mm)
target_include_directories(SplatThumb PRIVATE icon)
target_link_libraries(SplatThumb PRIVATE
    splatthumb_core
    "-framework Cocoa"
    "-framework CoreServices"
    "-framework ServiceManagement"
    "-framework UniformTypeIdentifiers")
set_target_properties(SplatThumb PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/app/Info.plist
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.voxelkei.splatthumb
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES
    XCODE_EMBED_APP_EXTENSIONS SplatThumbThumbnail
    XCODE_EMBED_APP_EXTENSIONS_CODE_SIGN_ON_COPY YES
    XCODE_EMBED_APP_EXTENSIONS_REMOVE_HEADERS_ON_COPY YES)
splatthumb_sign(SplatThumb ${CMAKE_CURRENT_SOURCE_DIR}/app/SplatThumb.entitlements)
add_dependencies(SplatThumb SplatThumbThumbnail)

# --- Option B spike: custom Finder icons ------------------------------------
add_executable(splatthumb-icon icon/main.mm icon/STIconSetter.mm)
target_link_libraries(splatthumb-icon PRIVATE
    splatthumb_core
    "-framework Foundation"
    "-framework AppKit"
    "-framework CoreServices")
set_target_properties(splatthumb-icon PROPERTIES
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-"
    XCODE_ATTRIBUTE_CLANG_ENABLE_OBJC_ARC YES)
