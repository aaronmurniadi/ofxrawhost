cmake_minimum_required(VERSION 3.16)
project(OfxRawHost LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# The layer graph: a source file may include its own layer and the ones below it,
# never one above. The targets catch this when a symbol has to resolve, which is
# too late for a header-only include or one whose symbol happens to resolve
# through another consumer. See cmake/check_layers.cmake.
execute_process(
  COMMAND ${CMAKE_COMMAND} -P ${CMAKE_SOURCE_DIR}/cmake/check_layers.cmake
  RESULT_VARIABLE _layer_check
  OUTPUT_VARIABLE _layer_output
  ERROR_VARIABLE _layer_output)
if(NOT _layer_check EQUAL 0)
  message(FATAL_ERROR "${_layer_output}")
endif()

# MSVC: keep windows.h from defining min/max macros (the code calls std::min and
# std::max), and read the UTF-8 sources and string literals as UTF-8.
if(MSVC)
  add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN _CRT_SECURE_NO_WARNINGS)
  add_compile_options(/utf-8)
  # vcpkg builds every dependency of a static triplet against the static C
  # runtime, so the host must match or the linker rejects the mixture.
  if(VCPKG_TARGET_TRIPLET MATCHES "-static$")
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  endif()
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
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl2.cpp
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

# --- Modules ---
# One target per layer, and every target links only downwards. A wrong-direction
# #include therefore fails to compile instead of passing review. See PLAN.md for
# the graph.
#
#   app/        composition root            -> everything
#   ui/         ImGui panels, GL, dialogs   -> services model engine gfx core
#   services/   use-cases, persistence      -> engine ofx io model core
#   engine/     chain rendering, scheduling -> ofx io model gfx core
#   ofx/        OpenFX SDK adapter          -> io core platform
#   io/         codecs, persist       -> core platform
#   model/      state units, no ImGui       -> core gfx
#   gfx/        GlTexture                   -> nothing
#   core/       Image, Json, version        -> nothing
add_library(ofxrawhost_core STATIC
  src/core/Image.cpp
  src/core/Json.cpp
  src/core/RenderContext.cpp
  src/core/Version.cpp
  src/core/UpdateChannel.cpp
)
target_include_directories(ofxrawhost_core PUBLIC src)

add_library(ofxrawhost_gfx STATIC src/gfx/GlTexture.cpp)
target_link_libraries(ofxrawhost_gfx PUBLIC ofxrawhost_core glfw)
if(APPLE)
  target_link_libraries(ofxrawhost_gfx PUBLIC "-framework OpenGL")
elseif(WIN32)
  target_link_libraries(ofxrawhost_gfx PUBLIC opengl32)
else()
  target_link_libraries(ofxrawhost_gfx PUBLIC OpenGL::GL)
endif()

add_library(ofxrawhost_platform STATIC
  src/platform/Console.cpp
  src/platform/EmbeddedResource.cpp
  src/platform/Http.cpp
  src/platform/Paths.cpp
  src/platform/SystemUiFont.cpp
  src/platform/Report.cpp
)
if(APPLE)
  target_sources(ofxrawhost_platform PRIVATE src/platform/HttpApple.mm)
endif()
target_link_libraries(ofxrawhost_platform PUBLIC ofxrawhost_core)

add_library(ofxrawhost_io STATIC
  src/codecs/ImageLoad.cpp
  src/codecs/ImageColor.cpp
  src/codecs/ImageWrite.cpp
  src/codecs/NoiseProfiles.cpp
  src/persist/ProjectPersistSave.cpp
  src/persist/ProjectPersistLoad.cpp
  third_party/tinyexr/tinyexr.cc
)
target_include_directories(ofxrawhost_io PRIVATE third_party/stb third_party/tinyexr)
target_compile_definitions(ofxrawhost_io PUBLIC
  "OFX_NOISE_PROFILE_PATH=\"${CMAKE_SOURCE_DIR}/third_party/noiseprofiles/noiseprofiles.json\"")
target_link_libraries(ofxrawhost_io PUBLIC ofxrawhost_core ofxrawhost_platform PRIVATE ZLIB::ZLIB)

if(TARGET PkgConfig::LIBRAW)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::LIBRAW)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${LIBRAW_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${LIBRAW_LIBRARY})
endif()
if(TARGET PkgConfig::LIBTIFF)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::LIBTIFF)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${LIBTIFF_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${LIBTIFF_LIBRARY})
endif()
if(TARGET PkgConfig::LCMS2)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::LCMS2)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${LCMS2_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${LCMS2_LIBRARY})
endif()
if(TARGET PkgConfig::WEBP)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::WEBP)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${WEBP_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${WEBP_LIBRARY})
endif()
if(TARGET PkgConfig::WEBPMUX)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::WEBPMUX)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${WEBP_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${WEBPMUX_LIBRARY})
endif()
if(TARGET PkgConfig::JXL)
  target_link_libraries(ofxrawhost_io PRIVATE PkgConfig::JXL)
else()
  target_include_directories(ofxrawhost_io PRIVATE ${JXL_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_io PRIVATE ${JXL_LIBRARY} ${JXL_THREADS_LIBRARY})
endif()

# The model layer is header-only state: Document, Chain, GuiState. It owns no
# translation unit, so it is an interface target.
add_library(ofxrawhost_model INTERFACE)
target_link_libraries(ofxrawhost_model INTERFACE ofxrawhost_core ofxrawhost_gfx)

add_library(ofxrawhost_ofx STATIC
  src/ofx/OfxSuites.cpp
  src/ofx/OfxPropsSuite.cpp
  src/ofx/OfxRuntimeSuites.cpp
  src/ofx/OfxPlugins.cpp
  src/ofx/SpektraPreset.cpp
)
if(APPLE)
  target_sources(ofxrawhost_ofx PRIVATE src/ofx/OfxMetal.mm)
else()
  target_sources(ofxrawhost_ofx PRIVATE src/ofx/OfxMetalStub.cpp)
endif()
target_include_directories(ofxrawhost_ofx PUBLIC third_party/openfx/include)
target_link_libraries(ofxrawhost_ofx PUBLIC ofxrawhost_io OfxSupport)
if(WIN32)
  # The Windows release compiles the bundled effects into the executable, so the
  # registry has to look for them. The switch belongs to the target that compiles
  # loadPlugins(), not to the executable that happens to link it.
  target_compile_definitions(ofxrawhost_ofx PRIVATE OFX_EMBEDDED_PLUGIN)
endif()
if(APPLE)
  target_link_libraries(ofxrawhost_ofx PUBLIC "-framework Metal")
endif()

add_library(ofxrawhost_engine STATIC
  src/engine/ChainRenderer.cpp
  src/engine/RenderWorker.cpp
  src/engine/ExportWorker.cpp
)
target_link_libraries(ofxrawhost_engine PUBLIC ofxrawhost_model ofxrawhost_ofx)

add_library(ofxrawhost_services STATIC
  src/services/Actions.cpp
  src/services/NodeGraph.cpp
  src/services/Export.cpp
  src/services/StateMapping.cpp
  src/services/ChainIO.cpp
  src/services/ParamBridge.cpp
  src/services/ParamEdit.cpp
  src/services/PluginPresets.cpp
  src/services/UpdateCheck.cpp
  src/services/Filmstrip.cpp
)
target_link_libraries(ofxrawhost_services PUBLIC ofxrawhost_engine)

add_library(ofxrawhost_ui STATIC
  src/ui/Themes.cpp
  src/ui/ImGuiBackend.cpp
  src/ui/DockLayout.cpp
  src/ui/UiFrame.cpp
  src/ui/Widgets.cpp
  src/ui/ParamWidgets.cpp
  src/ui/PluginsPanel.cpp
  src/ui/ParametersPanel.cpp
  src/ui/PreviewPanel.cpp
  src/ui/FilmstripPanel.cpp
)
if(APPLE)
  target_sources(ofxrawhost_ui PRIVATE src/ui/MacPinch.m)
endif()
target_include_directories(ofxrawhost_ui PUBLIC
  third_party/portable-file-dialogs
  third_party/fontawesome
  third_party/inter
)
target_compile_definitions(ofxrawhost_ui PUBLIC
  "OFX_ICON_FONT_PATH=\"${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf\""
  "OFX_UI_FONT_PATH=\"${CMAKE_SOURCE_DIR}/third_party/inter/Inter.ttf\""
)
target_link_libraries(ofxrawhost_ui PUBLIC ofxrawhost_services imgui_lib glfw)

add_executable(OfxRawHost
  src/app/main.cpp
  src/app/RunApp.cpp
)
target_link_libraries(OfxRawHost PRIVATE ofxrawhost_ui)

# The self-test is a separate binary on every platform whose release ships more
# than one file: it links the libraries, not the shipping executable, so it needs
# no window. The Windows release is deliberately a single .exe, so there the test
# objects are also linked into the app and --selftest runs them in place.
add_library(ofxrawhost_selftest OBJECT
  src/selftest/Selftest.cpp
  src/selftest/TestSupport.cpp
  src/selftest/TestImage.cpp
  src/selftest/TestState.cpp
  src/selftest/TestPlugins.cpp
  src/selftest/TestConcurrency.cpp
)
# The cases reach into the UI themes and into the codecs directly, so the test
# objects link the whole stack.
target_link_libraries(ofxrawhost_selftest PUBLIC ofxrawhost_ui)
if(TARGET PkgConfig::LIBTIFF)
  target_link_libraries(ofxrawhost_selftest PUBLIC PkgConfig::LIBTIFF)
else()
  target_include_directories(ofxrawhost_selftest PUBLIC ${LIBTIFF_INCLUDE_DIR})
  target_link_libraries(ofxrawhost_selftest PUBLIC ${LIBTIFF_LIBRARY})
endif()

add_executable(OfxRawHostTests src/selftest/TestMain.cpp $<TARGET_OBJECTS:ofxrawhost_selftest>)
target_link_libraries(OfxRawHostTests PRIVATE ofxrawhost_selftest)

# Every module, so a global switch below reaches the libraries as well as the
# two executables.
set(OFX_MODULE_TARGETS
  ofxrawhost_core
  ofxrawhost_gfx
  ofxrawhost_platform
  ofxrawhost_io
  ofxrawhost_ofx
  ofxrawhost_engine
  ofxrawhost_services
  ofxrawhost_ui
  ofxrawhost_selftest
  OfxRawHost
  OfxRawHostTests
)

function(ofx_module_compile_options)
  foreach(_ofx_target IN LISTS OFX_MODULE_TARGETS)
    target_compile_options(${_ofx_target} PRIVATE ${ARGN})
  endforeach()
endfunction()

function(ofx_module_compile_definitions)
  foreach(_ofx_target IN LISTS OFX_MODULE_TARGETS)
    target_compile_definitions(${_ofx_target} PRIVATE ${ARGN})
  endforeach()
endfunction()

# Link-time optimization (cross-TU inlining, dead-code elimination).
include(CheckIPOSupported)
check_ipo_supported(RESULT OFX_IPO_SUPPORTED OUTPUT OFX_IPO_OUTPUT)
if(OFX_IPO_SUPPORTED)
  set_property(TARGET ${OFX_MODULE_TARGETS} PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
else()
  message(STATUS "LTO not supported: ${OFX_IPO_OUTPUT}")
endif()

# SSE2 is the minimum SIMD baseline on x86_64 (always available there).
# On 32-bit x86, require it explicitly so stb_image / stbir can use SSE2 code paths.
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(i386|i486|i586|i686)$")
  ofx_module_compile_options( -msse2 -mfpmath=sse)
endif()

# Optional: optimize for the build host CPU (enables AVX, F16C, etc.).
option(OFX_NATIVE_ARCH "Build with -march=native for the host CPU" OFF)
if(OFX_NATIVE_ARCH)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    ofx_module_compile_options( -march=native)
  elseif(MSVC)
    ofx_module_compile_options( /arch:AVX2)
  endif()
  message(STATUS "Building with native architecture optimization")
endif()

# The include roots are PUBLIC on core, so every module inherits them, and the
# two executables only add the flag they need.
ofx_module_compile_definitions(GL_SILENCE_DEPRECATION)

add_custom_command(TARGET OfxRawHost POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf
    $<TARGET_FILE_DIR:OfxRawHost>/fa-solid-900.ttf
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/third_party/inter/Inter.ttf
    $<TARGET_FILE_DIR:OfxRawHost>/Inter.ttf
  # The sensor noise profiles sit beside the binary, like the fonts.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/third_party/noiseprofiles/noiseprofiles.json
    $<TARGET_FILE_DIR:OfxRawHost>/noiseprofiles.json
)

# The self-test reads the same files, so it finds them beside its own binary.
add_custom_command(TARGET OfxRawHostTests POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    ${CMAKE_SOURCE_DIR}/third_party/noiseprofiles/noiseprofiles.json
    $<TARGET_FILE_DIR:OfxRawHostTests>/noiseprofiles.json
)

# A vcpkg static build needs the dependencies of the libraries found above:
# JPEG XL pulls in highway and brotli, libtiff pulls in jpeg and lzma, and so on.
# find_library cannot express those, so link every library the vcpkg tree
# installed. Unused entries cost nothing, because the linker pulls a member only
# when a symbol demands it.
if(WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "-static$")
  file(GLOB OFX_VCPKG_STATIC_LIBS "${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib/*.lib")
  if(NOT OFX_VCPKG_STATIC_LIBS)
    message(FATAL_ERROR "No libraries under ${CMAKE_BINARY_DIR}/vcpkg_installed/${VCPKG_TARGET_TRIPLET}/lib for the static build")
  endif()
  target_link_libraries(OfxRawHost PRIVATE ${OFX_VCPKG_STATIC_LIBS})
  target_link_libraries(OfxRawHostTests PRIVATE ${OFX_VCPKG_STATIC_LIBS})
endif()

if(APPLE)
  target_link_libraries(OfxRawHost PRIVATE "-framework Cocoa" "-framework IOKit" "-framework CoreVideo" "-framework Metal" "-framework CoreText")
  target_link_libraries(OfxRawHostTests PRIVATE "-framework Cocoa" "-framework CoreText")
elseif(WIN32)
  # The update check reaches GitHub through WinHTTP library of Windows.
  target_link_libraries(OfxRawHost PRIVATE winhttp)
  target_link_libraries(OfxRawHostTests PRIVATE winhttp)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(OfxRawHost PRIVATE OpenGL::GL)
endif()

# --- Bundled OFX plugin bundle: OfxRawHost.ofx.bundle (Transform, Auto Exposure, Denoise) ---
find_package(Threads REQUIRED)

# The OpenFX Support library, with the one upstream fix described in the module.
include(cmake/PatchOfxSupport.cmake)

# Bundled plugins: the Transform, Auto Exposure, and Denoise (profiled) effects
# ship inside the Windows executable so the release is one file, and as an
# OfxRawHost.ofx.bundle everywhere else.
set(OFX_BUNDLED_PLUGIN_SOURCES
  plugins/Transform.cpp
  plugins/AutoExposure.cpp
  plugins/Denoise.cpp
  plugins/BundledPlugins.cpp
)

# Version shown in the About window. A dev build keeps the -dev suffix, for
# example 0.4.2-dev. The release build passes OFX_VERSION_LABEL as the git tag;
# a local build derives it from the nearest git tag.
set(OFX_VERSION_LABEL "" CACHE STRING "OfxRawHost version label")
if(OFX_VERSION_LABEL STREQUAL "")
  execute_process(COMMAND git describe --tags --abbrev=0
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE _ofx_version_tag OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
  set(OFX_VERSION_LABEL "${_ofx_version_tag}")
endif()
if(OFX_VERSION_LABEL MATCHES "^v(.*)$")
  set(OFX_VERSION_LABEL "${CMAKE_MATCH_1}")
endif()
if(OFX_VERSION_LABEL STREQUAL "")
  set(OFX_VERSION_LABEL "0.0.0")
endif()

# Numeric version for the Windows VERSIONINFO block.
set(OFX_VERSION "${OFX_VERSION_LABEL}")
if(OFX_VERSION MATCHES "^([0-9]+(\\.[0-9]+)*)")
  set(OFX_VERSION "${CMAKE_MATCH_1}")
else()
  message(WARNING "version label '${OFX_VERSION_LABEL}' is not numeric; using 0.0.0")
  set(OFX_VERSION "0.0.0")
endif()
target_compile_definitions(ofxrawhost_core PUBLIC OFXRAWHOST_VERSION="${OFX_VERSION_LABEL}")

if(WIN32)
  enable_language(RC)
  # VERSIONINFO takes four comma-separated numbers.
  string(REPLACE "." ";" _ofx_ver_parts "${OFX_VERSION}")
  list(LENGTH _ofx_ver_parts _ofx_ver_count)
  while(_ofx_ver_count LESS 4)
    list(APPEND _ofx_ver_parts 0)
    list(LENGTH _ofx_ver_parts _ofx_ver_count)
  endwhile()
  string(REPLACE ";" "," OFX_VERSION_COMMAS "${_ofx_ver_parts}")
  set(OFX_ICON_FONT_PATH "${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf")
  set(OFX_UI_FONT_PATH "${CMAKE_SOURCE_DIR}/third_party/inter/Inter.ttf")
  set(OFX_NOISE_PROFILE_PATH "${CMAKE_SOURCE_DIR}/third_party/noiseprofiles/noiseprofiles.json")
  set(OFX_APP_ICON_PATH "${CMAKE_SOURCE_DIR}/packaging/windows/ofxrawhost.ico")
  if(NOT EXISTS "${OFX_APP_ICON_PATH}")
    message(FATAL_ERROR "windows icon not found: ${OFX_APP_ICON_PATH}")
  endif()
  configure_file(packaging/windows/OfxRawHost.rc.in "${CMAKE_CURRENT_BINARY_DIR}/OfxRawHost.rc" @ONLY)

  target_sources(OfxRawHost PRIVATE ${OFX_BUNDLED_PLUGIN_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/OfxRawHost.rc")
  # This release is one file, so --selftest runs inside it instead of forwarding
  # to a sibling test binary.
  target_sources(OfxRawHost PRIVATE $<TARGET_OBJECTS:ofxrawhost_selftest>)
  target_link_libraries(OfxRawHost PRIVATE OfxSupport)
  target_compile_definitions(OfxRawHost PRIVATE OFX_SELFTEST_IN_APP)
  # The standalone test binary carries the same factories, so it needs no
  # installed plugin to pass.
  target_sources(OfxRawHostTests PRIVATE ${OFX_BUNDLED_PLUGIN_SOURCES})
  target_link_libraries(OfxRawHostTests PRIVATE OfxSupport)
  # Release build opens no console window. The host keeps main(), so name the CRT
  # entry point explicitly instead of adding a WinMain wrapper.
  set_target_properties(OfxRawHost PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(OfxRawHost PRIVATE /ENTRY:mainCRTStartup)
else()
  # Binary must be named OfxRawHost.ofx: the host derives it from the bundle
  # stem (OfxRawHost.ofx.bundle -> Contents/<arch>/OfxRawHost.ofx).
  add_library(OfxRawHostBundled MODULE ${OFX_BUNDLED_PLUGIN_SOURCES})
  set_target_properties(OfxRawHostBundled PROPERTIES PREFIX "" OUTPUT_NAME "OfxRawHost" SUFFIX ".ofx")
  target_link_libraries(OfxRawHostBundled PRIVATE OfxSupport)
  if(APPLE)
    target_link_libraries(OfxRawHostBundled PRIVATE "-framework OpenGL")
  else()
    find_package(OpenGL REQUIRED)
    target_link_libraries(OfxRawHostBundled PRIVATE OpenGL::GL)
  endif()

  # Arch subdir must match pluginBinary() in src/ofx/OfxPlugins.cpp.
  if(APPLE)
    set(OFX_PLUGIN_ARCH "MacOS")
  elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
    set(OFX_PLUGIN_ARCH "Linux-arm-64")
  else()
    set(OFX_PLUGIN_ARCH "Linux-x86-64")
  endif()

  set(OFX_BUNDLE "${CMAKE_BINARY_DIR}/Plugins/OfxRawHost.ofx.bundle")
  add_custom_command(TARGET OfxRawHostBundled POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${OFX_BUNDLE}/Contents/${OFX_PLUGIN_ARCH}"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:OfxRawHostBundled>"
      "${OFX_BUNDLE}/Contents/${OFX_PLUGIN_ARCH}/OfxRawHost.ofx"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${CMAKE_SOURCE_DIR}/plugins/Info.plist" "${OFX_BUNDLE}/Contents/Info.plist"
    COMMENT "Assembling OfxRawHost.ofx.bundle"
  )
  add_dependencies(OfxRawHost OfxRawHostBundled)
endif()

# macOS app bundle packaging (optional convenience target)
if(APPLE)
  add_custom_target(app ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/PlugIns
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OfxRawHost>
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS/OfxRawHost
    # --selftest forwards to this binary, so it travels inside the bundle.
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OfxRawHostTests>
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS/OfxRawHostTests
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/third_party/fontawesome/fa-solid-900.ttf
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources/fa-solid-900.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/third_party/inter/Inter.ttf
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources/Inter.ttf
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      ${CMAKE_SOURCE_DIR}/third_party/noiseprofiles/noiseprofiles.json
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources/noiseprofiles.json
    COMMAND ${CMAKE_COMMAND} -E copy_directory
      ${OFX_BUNDLE}
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/PlugIns/OfxRawHost.ofx.bundle
    COMMAND ${CMAKE_COMMAND} -E echo "Built ${CMAKE_BINARY_DIR}/OfxRawHost.app"
    DEPENDS OfxRawHost OfxRawHostTests OfxRawHostBundled
  )
endif()
