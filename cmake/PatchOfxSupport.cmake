# Builds the OpenFX Support library from third_party/openfx with one change.
#
# The support lib's log-indent counter is a process-global int (upstream calls it
# "not MP sane"), and action dispatches from the render and the UI threads race
# on it. This patches a private copy to thread_local so the submodule stays
# pristine. The anchor check keeps the patch honest: if upstream renames the
# line, configuring fails instead of silently building the racy version.

set(OFX_SUPPORT_SRC "${CMAKE_SOURCE_DIR}/third_party/openfx")
set(OFX_SUPPORT_PATCHED_LOG "${CMAKE_CURRENT_BINARY_DIR}/ofxsLog.cpp")

file(READ "${OFX_SUPPORT_SRC}/Support/Library/ofxsLog.cpp" _ofxs_log)
string(REPLACE
  "static int gIndent = 0;"
  "static thread_local int gIndent = 0;"
  _ofxs_log "${_ofxs_log}")
if(NOT _ofxs_log MATCHES "static thread_local int gIndent = 0;")
  message(FATAL_ERROR "ofxsLog.cpp gIndent patch anchor not found; update cmake/PatchOfxSupport.cmake")
endif()
file(WRITE "${OFX_SUPPORT_PATCHED_LOG}" "${_ofxs_log}")

add_library(OfxSupport STATIC
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsCore.cpp"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsImageEffect.cpp"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsInteract.cpp"
  "${OFX_SUPPORT_PATCHED_LOG}"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsMultiThread.cpp"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsParams.cpp"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsProperty.cpp"
  "${OFX_SUPPORT_SRC}/Support/Library/ofxsPropertyValidation.cpp"
)
target_include_directories(OfxSupport PUBLIC
  "${OFX_SUPPORT_SRC}/include"
  "${OFX_SUPPORT_SRC}/Support/include"
)
target_link_libraries(OfxSupport PUBLIC Threads::Threads)
set_target_properties(OfxSupport PROPERTIES POSITION_INDEPENDENT_CODE ON)
