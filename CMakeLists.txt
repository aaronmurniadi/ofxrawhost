cmake_minimum_required(VERSION 3.16)
project(OfxRawHost LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(FetchContent)

# --- GLFW ---
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4)
FetchContent_MakeAvailable(glfw)

# --- Dear ImGui ---
FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui.git GIT_TAG v1.91.8)
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

add_executable(OfxRawHost
  src/main.cpp
  src/OfxHost.cpp
  src/ImageIO.cpp
  src/UI.cpp
  third_party/tinyexr/tinyexr.cc
)

target_include_directories(OfxRawHost PRIVATE
  src
  third_party/openfx/include
  third_party/stb
  third_party/tinyexr
  third_party/portable-file-dialogs
)

if(TARGET PkgConfig::LIBRAW)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::LIBRAW)
else()
  target_include_directories(OfxRawHost PRIVATE ${LIBRAW_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${LIBRAW_LIBRARY})
endif()

if(TARGET PkgConfig::LCMS2)
  target_link_libraries(OfxRawHost PRIVATE PkgConfig::LCMS2)
else()
  target_include_directories(OfxRawHost PRIVATE ${LCMS2_INCLUDE_DIR})
  target_link_libraries(OfxRawHost PRIVATE ${LCMS2_LIBRARY})
endif()

target_link_libraries(OfxRawHost PRIVATE imgui_lib glfw ZLIB::ZLIB)

if(APPLE)
  target_link_libraries(OfxRawHost PRIVATE "-framework OpenGL" "-framework Cocoa" "-framework IOKit" "-framework CoreVideo")
elseif(WIN32)
  target_link_libraries(OfxRawHost PRIVATE opengl32)
else()
  find_package(OpenGL REQUIRED)
  target_link_libraries(OfxRawHost PRIVATE OpenGL::GL)
endif()

# macOS app bundle packaging (optional convenience target)
if(APPLE)
  add_custom_target(app ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/Resources
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:OfxRawHost>
      ${CMAKE_BINARY_DIR}/OfxRawHost.app/Contents/MacOS/OfxRawHost
    COMMAND ${CMAKE_COMMAND} -E echo "Built ${CMAKE_BINARY_DIR}/OfxRawHost.app"
    DEPENDS OfxRawHost
  )
endif()
