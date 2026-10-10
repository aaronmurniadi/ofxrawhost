# Mirrors the builtin x64-windows-static triplet, with the debug variant turned
# off. The release workflow only builds the Release configuration, so a debug
# library set would double the vcpkg time and take twice the package cache for
# something nothing links against.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PROVIDED_FORTRAN ON)
set(VCPKG_BUILD_TYPE release)
