# Fails the configure step when a source file includes a layer above its own.
#
# The targets already enforce this when a symbol has to resolve, but that is a
# link-time check: a header-only include, or one whose symbol happens to resolve
# through another consumer, passes. model/Filmstrip.cpp did exactly that for
# several releases.
#
# Run from CMakeLists.txt, build.sh, and the release workflow:
#   cmake -P cmake/check_layers.cmake

cmake_minimum_required(VERSION 3.16)

set(root "${CMAKE_CURRENT_LIST_DIR}/..")
set(src "${root}/src")

# Every layer, and the layers it may include. Anything not listed here is upward.
# The io target owns two directories, codecs/ and persist/, and persist may use
# codecs because the writer needs the file formats.
set(allowed_core "")
set(allowed_platform "core")
set(allowed_gfx "core")
set(allowed_model "core;gfx")
set(allowed_codecs "core;platform")
set(allowed_persist "core;platform;codecs")
set(allowed_ofx "core;platform")
set(allowed_engine "core;gfx;model;codecs;persist;ofx")
set(allowed_services "core;platform;gfx;model;codecs;persist;ofx;engine")
set(allowed_ui "core;platform;gfx;model;codecs;persist;ofx;engine;services")
set(allowed_app "core;platform;gfx;model;codecs;persist;ofx;engine;services;ui;selftest")

set(layers core platform gfx model codecs persist ofx engine services ui app selftest)

# Documented exceptions: the Windows in-app self-test, and the interface's own
# window and preview texture. Each is one edge and each is a deliberate choice.
set(exceptions
  "src/app/main.cpp:selftest"
  "src/ui/UiFrame.h:app"

  # The document header reaches into the codec module for ColorSpace, which now
  # lives in core/Image.h. Listed so the gate can run before that is fixed.
)

file(GLOB_RECURSE sources
  "${src}/*.h" "${src}/*.hpp" "${src}/*.cpp" "${src}/*.c" "${src}/*.mm" "${src}/*.m")

list(SORT sources)
set(violations "")

foreach(path IN LISTS sources)
  file(RELATIVE_PATH rel "${root}" "${path}")

  # Which layer this file belongs to, from src/<layer>/...
  string(REGEX MATCH "^src/([a-z]+)/" own_match "${rel}")
  if(NOT own_match)
    continue()
  endif()
  set(own "${CMAKE_MATCH_1}")
  if(NOT own IN_LIST layers)
    continue()
  endif()
  if(own STREQUAL "selftest")
    continue()  # the test binary links every layer; it has no layer of its own
  endif()
  set(allowed_var "allowed_${own}")
  set(allowed "${${allowed_var}}")

  file(STRINGS "${path}" includes REGEX "^[ \t]*#[ \t]*include[ \t]*\"[a-z]+/")
  foreach(line IN LISTS includes)
    string(REGEX MATCH "#include[ \t]*\"([a-z]+)/" inc_match "${line}")
    if(NOT inc_match)
      continue()
    endif()
    set(included "${CMAKE_MATCH_1}")
    if(NOT included IN_LIST layers)
      continue()
    endif()
    if(included STREQUAL own)
      continue()
    endif()
    if(included IN_LIST allowed)
      continue()
    endif()
    if("${rel}:${included}" IN_LIST exceptions)
      continue()
    endif()
    list(APPEND violations "${rel} is in ${own}/ and includes ${included}/")
  endforeach()
endforeach()

if(violations)
  list(LENGTH violations count)
  message(FATAL_ERROR
    "Layer check failed: ${count} upward include(s).\n"
    "  A file may only include its own layer and the layers below it.\n"
    "  Fix the edge or add it to the exceptions list in cmake/check_layers.cmake.\n"
    "  ${violations}")
endif()
