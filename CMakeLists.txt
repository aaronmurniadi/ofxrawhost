cmake_minimum_required(VERSION 3.16)
project(OfxRawHost LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# MSVC: keep windows.h from defining min/max macros (the code calls std::min and
# std::max), and read the UTF-8 sources and string literals as UTF-8.
if(MSVC)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  add_compile_options(/utf-8)
endif()

include(FetchContent)

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui ---
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.8-docking)
FetchContent_MakeAvailable(imgui)
add_library(imgui_lib
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${imgui_SOURCE_DIR}/imgui_draw.cpp
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui_lib PUBLIC ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
target_link_libraries(imgui_lib PUBLIC glfw)

# --- LibRaw ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LIBRAW IMPORTED_TARGET libraw)
endif()
if(NOT LIBRAW_FOUND)
  find_path(LIBRAW_INCLUDE_DIR libraw/libraw.h)
  find_library(LIBRAW_LIBRARY NAMES raw raw_r)
  if(LIBRAW_INCLUDE_DIR AND LIBRAW_LIBRARY)
    set(LIBRAW_FOUND TRUE)
  endif()
endif()
if(NOT LIBRAW_FOUND)
  message(FATAL_ERROR "LibRaw not found. Install libraw (e.g. brew install libraw / apt install libraw-dev).")
endif()

find_package(ZLIB REQUIRED)

# --- libtiff ---
if(PkgConfig_FOUND)
  pkg_check_modules(LIBTIFF IMPORTED_TARGET libtiff-4)
endif()
if(NOT LIBTIFF_FOUND)
  find_path(LIBTIFF_INCLUDE_DIR tiffio.h)
  find_library(LIBTIFF_LIBRARY NAMES tiff)
  if(LIBTIFF_INCLUDE_DIR AND LIBTIFF_LIBRARY)
    set(LIBTIFF_FOUND TRUE)
  endif()
endif()
if(NOT LIBTIFF_FOUND)
  message(FATAL_ERROR "libtiff not found. Install libtiff (e.g. brew install libtiff / apt install libtiff-dev).")
endif()

# --- Little CMS (ICC) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(LCMS2 IMPORTED_TARGET lcms2)
endif()
if(NOT LCMS2_FOUND)
  find_path(LCMS2_INCLUDE_DIR lcms2.h)
  find_library(LCMS2_LIBRARY NAMES lcms2)
  if(LCMS2_INCLUDE_DIR AND LCMS2_LIBRARY)
    set(LCMS2_FOUND TRUE)
  endif()
endif()
if(NOT LCMS2_FOUND)
  message(FATAL_ERROR "lcms2 not found. Install little-cms2 (e.g. brew install little-cms2 / apt install liblcms2-dev).")
endif()

# --- libwebp (WebP export) ---
if(PkgConfig_FOUND)
  pkg_check_modules(WEBP IMPORTED_TARGET libwebp)
  pkg_check_modules(WEBPMUX IMPORTED_TARGET libwebpmux)
endif()
if(NOT (WEBP_FOUND AND WEBPMUX_FOUND))
  find_path(WEBP_INCLUDE_DIR webp/encode.h)
  find_library(WEBP_LIBRARY NAMES webp)
  find_library(WEBPMUX_LIBRARY NAMES webpmux)
  if(WEBP_INCLUDE_DIR AND WEBP_LIBRARY AND WEBPMUX_LIBRARY)
    set(WEBP_FOUND TRUE)
    set(WEBPMUX_FOUND TRUE)
  elseif(NOT WEBPMUX_FOUND)
    set(WEBP_FOUND FALSE)
  endif()
endif()
if(NOT WEBP_FOUND)
  message(FATAL_ERROR "libwebp not found. Install webp (e.g. brew install webp / apt install libwebp-dev).")
endif()

# --- libjxl (JPEG XL export) ---
if(PkgConfig_FOUND)
  pkg_check_modules(JXL IMPORTED_TARGET libjxl libjxl_threads)
endif()
if(NOT JXL_FOUND)
  find_path(JXL_INCLUDE_DIR jxl/encode.h)
  find_library(JXL_LIBRARY NAMES jxl)
  find_library(JXL_THREADS_LIBRARY NAMES jxl_threads)
  if(JXL_INCLUDE_DIR AND JXL_LIBRARY AND JXL_THREADS_LIBRARY)
    set(JXL_FOUND TRUE)
  endif()
endif()
if(NOT JXL_FOUND)
  message(FATAL_ERROR "libjxl not found. Install JPEG XL (e.g. brew install jpeg-xl / apt install libjxl-dev).")
endif()

add_executable(OfxRawHost
  src/main.cpp
  src/selftest/Selftest.cpp
  src/platform/Paths.cpp
  src/ofx/OfxSuites.cpp
  src/ofx/OfxPlugins.cpp
  src/imgio/ImageLoad.cpp
  src/imgio/ImageColor.cpp
  src/imgio/ImageWrite.cpp
  src/UI.cpp
  src/NodeGraph.cpp
  src/ParamBridge.cpp
  src/ChainRenderer.cpp
  src/RenderScheduler.cpp
  src/Actions.cpp
  src/persist/DocumentActions.cpp
  src/persist/ChainIO.cpp
  src/persist/Json.cpp
  src/persist/ProjectPersistSave.cpp
  src/persist/ProjectPersistLoad.cpp
  src/ui/Themes.cpp
  src/ui/GlTexture.cpp
  src/Filmstrip.cpp
  src/ui/ImGuiBackend.cpp
  src/ui/DockLayout.cpp
  src/ui/UiFrame.cpp
  src/ui/Widgets.cpp
  src/ui/ParamWidgets.cpp
  src/ui/ParamEdit.cpp
  src/ui/LeftPanel.cpp
  src/ui/RightPanel.cpp
  src/ui/PreviewPanel.cpp
  src/ui/FilmstripPanel.cpp
  third_party/tinyexr/tinyexr.cc
)

if(APPLE)
  target_sources(OfxRawHost PRIVATE src/ui/MacPinch.m src/ofx/OfxMetal.mm)
else()
  target_sources(OfxRawHost PRIVATE src/ofx/OfxMetalStub.cpp)
endif()

# Link-time optimization (cross-TU inlining, dead-code elimination).
include(CheckIPOSupported)
check_ipo_supported(RESULT OFX_IPO_SUPPORTED OUTPUT OFX_IPO_OUTPUT)
if(OFX_IPO_SUPPORTED)
  set_property(TARGET OfxRawHost PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  message(STATUS "LTO not supported: ${OFX_IPO_OUTPUT}")
endif()

# SSE2 is the minimum SIMD baseline on x86_64 (always available there).
# On 32-bit x86, require it explicitly so stb_image / stbir can use SSE2 code paths.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i486|i586|i686)$")
  target_compile_options(OfxRawHost PRIVATE -msse2 -mfpmath=sse)
endif()

# Optional: optimize for the build host CPU (enables AVX, F16C, etc.).
option(OFX_NATIVE_ARCH "Build with -march=native for the host CPU" OFF)
if(OFX_NATIVE_ARCH)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(OfxRawHost PRIVATE -march=native)
  elseif(MSVC)
    target_compile_options(OfxRawHost PRIVATE /arch:AVX2)
  endif()
  message(STATUS "Building with native architecture optimization")
endif()

target_include_directories(OfxRawHost PRIVATE
  src
  third_party/openfx/include
  third_party/stb
  third_party/tinyexr
  third_party/portable-file-dialogs
  third_party/fontawesome
)

target_compile_definitions(OfxRawHost PRIVATE
  "OFX_ICON_FONT_PATH=\"${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf\""
  GL_SILENCE_DEPRECATION
)

add_custom_command(TARGET OfxRawHost POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf
    $<TARGET_FILE_DIR:OfxRawHost>/fa-solid-900.ttf
)

if(TARGET PkgConfig::LIBRAW)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::LIBRAW)
else()
  target_include_directories(OfxRawHost PRIVATE ${LIBRAW_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${LIBRAW_LIBRARY})
endif()

if(TARGET PkgConfig::LIBTIFF)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::LIBTIFF)
else()
  target_include_directories(OfxRawHost PRIVATE ${LIBTIFF_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${LIBTIFF_LIBRARY})
endif()

if(TARGET PkgConfig::LCMS2)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::LCMS2)
else()
  target_include_directories(OfxRawHost PRIVATE ${LCMS2_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${LCMS2_LIBRARY})
endif()

if(TARGET PkgConfig::WEBP)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::WEBP)
else()
  target_include_directories(OfxRawHost PRIVATE ${WEBP_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${WEBP_LIBRARY})
endif()

if(TARGET PkgConfig::WEBPMUX)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::WEBPMUX)
else()
  target_include_directories(OfxRawHost PRIVATE ${WEBP_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${WEBPMUX_LIBRARY})
endif()

if(TARGET PkgConfig::JXL)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::JXL)
else()
  target_include_directories(OfxRawHost PRIVATE ${JXL_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${JXL_LIBRARY} ${JXL_THREADS_LIBRARY})
endif()

target_link_libraries(OfxRawHost PRIVATE imgui_lib glfw ZLIB::ZLIB)

if(APPLE)
  target_link_libraries(OfxRawHost PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework Metal")
elseif(WIN32)
  target_link_libraries(OfxRawHost PRIVATE opengl32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(OfxRawHost PRIVATE OpenGL::GL)
endif()

# --- Bundled OFX plugin bundle: OfxRawHost.ofx.bundle (Transform) ---
find_package(Threads REQUIRED)

# The support lib's log-indent counter is a process-global int (upstream calls it
# "not MP sane"); action dispatches from the render and UI threads race on it.
# Patch a private copy to thread_local so the submodule stays pristine.
file(READ third_party/openfx/Support/Library/ofxsLog.cpp _ofxsLog)
string(REPLACE "static int gIndent = 0;" "static thread_local int gIndent = 0;" _ofxsLog "${_ofxsLog}")
if(NOT _ofxsLog MATCHES "static thread_local int gIndent = 0;")
  message(FATAL_ERROR "ofxsLog.cpp gIndent patch anchor not found; update the CMakeLists.txt patch")
endif()
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/ofxsLog.cpp" "${_ofxsLog}")

add_library(OfxSupport STATIC
  third_party/openfx/Support/Library/ofxsCore.cpp
  third_party/openfx/Support/Library/ofxsImageEffect.cpp
  third_party/openfx/Support/Library/ofxsInteract.cpp
  ${CMAKE_CURRENT_BINARY_DIR}/ofxsLog.cpp
  third_party/openfx/Support/Library/ofxsMultiThread.cpp
  third_party/openfx/Support/Library/ofxsParams.cpp
  third_party/openfx/Support/Library/ofxsProperty.cpp
  third_party/openfx/Support/Library/ofxsPropertyValidation.cpp
)
target_include_directories(OfxSupport PUBLIC
  third_party/openfx/include
  third_party/openfx/Support/include
)
target_link_libraries(OfxSupport PUBLIC Threads::Threads)
set_target_properties(OfxSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Binary must be named OfxRawHost.ofx: the host derives it from the bundle
# stem (OfxRawHost.ofx.bundle -> Contents/<arch>/OfxRawHost.ofx).
add_library(OfxRawHostTransform MODULE plugins/Transform.cpp)
set_target_properties(OfxRawHostTransform PROPERTIES PREFIX "" OUTPUT_NAME "OfxRawHost" SUFFIX ".ofx")
target_link_libraries(OfxRawHostTransform PRIVATE OfxSupport)
if(APPLE)
  target_link_libraries(OfxRawHostTransform PRIVATE "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(OfxRawHostTransform PRIVATE opengl32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(OfxRawHostTransform PRIVATE OpenGL::GL)
endif()

# Arch subdir must match pluginBinary() in src/ofx/OfxPlugins.cpp.
if(APPLE)
  set(OFX_PLUGIN_ARCH "MacOS")
elseif(WIN32)
  if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(OFX_PLUGIN_ARCH "Win64")
  else()
    set(OFX_PLUGIN_ARCH "Win32")
  endif()
elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  set(OFX_PLUGIN_ARCH "Linux-arm-64")
else()
  set(OFX_PLUGIN_ARCH "Linux-x86-64")
endif()

set(OFX_BUNDLE "${CMAKE_BINARY_DIR}/Plugins/OfxRawHost.ofx.bundle")
add_custom_command(TARGET OfxRawHostTransform POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E make_directory "${OFX_BUNDLE}/Contents/${OFX_PLUGIN_ARCH}"
  COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OfxRawHostTransform>"
    "${OFX_BUNDLE}/Contents/${OFX_PLUGIN_ARCH}/OfxRawHost.ofx"
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_SOURCE_DIR}/plugins/Info.plist" "${OFX_BUNDLE}/Contents/Info.plist"
  COMMENT "Assembling OfxRawHost.ofx.bundle"
)
add_dependencies(OfxRawHost OfxRawHostTransform)

# macOS app bundle packaging (optional convenience target)
if(APPLE)
  add_custom_target(app ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/PlugIns
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OfxRawHost>
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS/OfxRawHost
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources/fa-solid-900.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${OFX_BUNDLE}
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/PlugIns/OfxRawHost.ofx.bundle
    COMMAND ${CMAKE_COMMAND} -E echo "Built ${CMAKE_BINARY_DIR}/OfxRawHost.app"
    DEPENDS OfxRawHost OfxRawHostTransform
  )
endif()
