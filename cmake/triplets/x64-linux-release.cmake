set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CMAKE_SYSTEM_NAME Linux)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

# 发布流水线不需要 Debug 依赖；只构建 Release 可显著减少首次编译时间和缓存体积。
set(VCPKG_BUILD_TYPE release)
