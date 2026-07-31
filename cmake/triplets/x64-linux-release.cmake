set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# Mapnik 4.3 only produces loadable .input modules when shared plugins are
# enabled. A dynamic library triplet keeps Mapnik and its plugins ABI-compatible.
# The release pipeline does not need Debug dependencies.
set(VCPKG_BUILD_TYPE release)
