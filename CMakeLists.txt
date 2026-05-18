cmake_minimum_required(VERSION 3.31)

option(ENABLE_SANITIZERS "Enable AddressSanitizer and UBSan" OFF)
option(POST_BUILD_COPY_EXT_LIBS "Copy external libraries to build directory" ON)

message(STATUS "ENABLE_SANITIZERS: ${ENABLE_SANITIZERS}")

file(READ src/core/vpversion.h version)
string(REGEX MATCH "VP_VERSION_MAJOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "VP_VERSION_MINOR[ ]+([0-9]+)" _tmp ${version})
set(VERSION_MINOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "VP_VERSION_REV[ ]+([0-9]+)" _tmp ${version})
set(VERSION_REV "${CMAKE_MATCH_1}")
set(APP_NAME "VPinballX_BGFX")

project(vpinball
   VERSION "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REV}"
   LANGUAGES C CXX OBJC OBJCXX Swift
)

# Personal-use build — no Developer ID, no notarisation. The Xcode generator
# tries to sign every dylib by default; turn it off so the build doesn't
# require a signing identity. These CMAKE_XCODE_ATTRIBUTE_* variables apply
# to every target created after this point.
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "")

include("${CMAKE_SOURCE_DIR}/make/CMakeLists_common_standalone.txt")

set(CMAKE_OSX_DEPLOYMENT_TARGET 26.0)
set(CMAKE_OSX_ARCHITECTURES arm64)

set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)
set(CMAKE_INSTALL_RPATH "@executable_path/../Frameworks")

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_C_STANDARD 99)

if(ENABLE_SANITIZERS)
   add_compile_options(-fsanitize=address)
   add_link_options(-fsanitize=address)

   add_compile_options(-fsanitize=undefined)
   add_link_options(-fsanitize=undefined)
endif()

add_executable(vpinball
   ${VPX_STANDALONE_SOURCES}

   src/input/OpenPinDevHandler.h
   src/input/OpenPinDevHandler.cpp
   src/input/GCInputHandler.h
   src/input/GCInputHandler.cpp
   src/input/GCInputHandlerBridge.h
   src/input/GCInputHandler_objc.mm

   standalone/macos/CBridge.h
   standalone/macos/CBridge.mm
   standalone/macos/ImageIOBridge.h
   standalone/macos/ImageIOBridge.mm
   standalone/macos/SettingsBridge.h
   standalone/macos/SettingsBridge.cpp
   standalone/macos/VPXApp.swift
   standalone/macos/VPXLauncher.swift
   standalone/macos/MetalViewHost.swift
   standalone/macos/InputForwarder.swift
   standalone/macos/SettingsRoot.swift
   standalone/macos/DisplaySettingsView.swift
   standalone/macos/ViewSettingsView.swift

   third-party/include/imgui/imgui_impl_sdl3.cpp
   third-party/include/imgui/imgui_impl_sdl3.h
)

target_include_directories(vpinball PUBLIC
   ${CMAKE_SOURCE_DIR}/third-party/include
   ${CMAKE_SOURCE_DIR}
   ${CMAKE_SOURCE_DIR}/third-party/include/libwinevbs/atl/include
   ${CMAKE_SOURCE_DIR}/third-party/include/libwinevbs/atlmfc/include
   ${CMAKE_SOURCE_DIR}/third-party/include/libwinevbs/wine/include
   media
   pin
   src
   plugins
)

target_precompile_headers(vpinball PUBLIC
)

target_compile_definitions(vpinball PRIVATE
   __STANDALONE__

   ENABLE_BGFX

   __LIBWINEVBS__
   __WINESRC__
   WINE_UNICODE_NATIVE

   $<$<CONFIG:Debug>:BX_CONFIG_DEBUG=1>
   $<$<CONFIG:Release>:BX_CONFIG_DEBUG=0>

   "__forceinline=__attribute__((always_inline)) inline"
)

target_compile_options(vpinball PUBLIC
   $<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-Werror=deprecated-declarations>
   # The three suppressions below are noise from libwinevbs COM-bridge code
   # (STDMETHOD subclasses missing override, throw() specs, legacy 64->32
   # truncations). Phase 2 of the modernisation roadmap replaces libwinevbs
   # with native Apple frameworks, which will eliminate the need for these.
   $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wno-inconsistent-missing-override>
   $<$<COMPILE_LANGUAGE:CXX,OBJCXX>:-Wno-missing-exception-spec>
   $<$<AND:$<OR:$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>,$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>>:
      -fdenormal-fp-math=preserve-sign
      -freciprocal-math
      -fassociative-math
      -fapprox-func
      -fno-math-errno
      -fno-trapping-math
      -ffp-contract=off
      -Wno-shorten-64-to-32>
   $<$<AND:$<CXX_COMPILER_ID:GNU>,$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>>:
      -ffast-math
      -ffp-contract=off>

   # Swift-specific: import CBridge.h as if it were a Swift module so the
   # C declarations there appear in Swift code with their C names. Same
   # mechanism as Xcode's SWIFT_OBJC_BRIDGING_HEADER setting, but works
   # under the Unix Makefiles / Ninja generators too. The SHELL: prefix
   # tells cmake to pass the two arguments separately rather than gluing
   # them into one quoted string.
   "$<$<COMPILE_LANGUAGE:Swift>:SHELL:-import-objc-header ${CMAKE_SOURCE_DIR}/standalone/macos/CBridge.h>"
)

target_link_directories(vpinball PUBLIC
   "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/macos-arm64"
)

target_link_libraries(vpinball PUBLIC
   bgfx
   SDL3
   SDL3_image
   SDL3_ttf
   freeimage
   hidapi
   winevbs
   "-framework AppKit"
   "-framework GameController"
)

# Configure Info.plist at configure time: substitutes @CMAKE_PROJECT_VERSION@
# so the bundle ships with real CFBundleShortVersionString/CFBundleVersion
# values rather than the empty strings that the Xcode generator strips out.
# This replaces the post-build PlistBuddy dance the project used to do.
configure_file(
   "${CMAKE_SOURCE_DIR}/standalone/macos/Info_BGFX.plist.in"
   "${CMAKE_CURRENT_BINARY_DIR}/Info_BGFX.plist"
   @ONLY
)

set_target_properties(vpinball PROPERTIES
   RUNTIME_OUTPUT_NAME "${APP_NAME}"
   MACOSX_BUNDLE YES
   MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info_BGFX.plist"
)

add_custom_command(TARGET vpinball POST_BUILD
   COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/src/assets" "$<TARGET_FILE_DIR:vpinball>/../Resources/assets"
   COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/scripts" "$<TARGET_FILE_DIR:vpinball>/../Resources/scripts"
   COMMAND "${CMAKE_COMMAND}" -E copy_directory "${CMAKE_SOURCE_DIR}/docs" "$<TARGET_FILE_DIR:vpinball>/../Resources/docs"
   COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:vpinball>/../Resources/shaders-${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_REV}"
   COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:vpinball>/../Frameworks"
   COMMAND cp -a "${CMAKE_SOURCE_DIR}/third-party/runtime-libs/macos-arm64/*.dylib" "$<TARGET_FILE_DIR:vpinball>/../Frameworks"
   COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/standalone/macos/VPinballX.icns" "$<TARGET_FILE_DIR:vpinball>/../Resources"
)

set(PluginPlatform "macos")
set(PluginArch "arm64")
include("${CMAKE_SOURCE_DIR}/make/CMakeLists_plugins.txt")
