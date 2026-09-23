#!/usr/bin/env bash
# macOS developer helper: builds the missing driver locally; end users need no dependencies.
set -euo pipefail
project_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps="$project_dir/.test-deps"
mkdir -p "$deps"
version="$(gdal-config --version)"
if [[ "$version" != "3.13.3" ]]; then
    echo 'This verified plugin recipe requires GDAL 3.13.3; use matching source for another ABI.' >&2
    exit 1
fi
fetch() {
    local url="$1" destination="$2" digest="$3"
    [[ -f "$destination" ]] || curl --fail --location --retry 2 "$url" -o "$destination"
    echo "$digest  $destination" | shasum -a 256 -c -
}
fetch 'https://github.com/HDFGroup/hdf4/archive/refs/tags/hdf4.3.1.tar.gz' "$deps/hdf4.tar.gz" 6dc3b8af610526788bf78fb3982b25a80abfc94e37ce0c3ae2929b5e9c937093
fetch 'https://github.com/OSGeo/gdal/releases/download/v3.13.3/gdal-3.13.3.tar.gz' "$deps/gdal.tar.gz" 5e0c388d83da2d686cc00a40272882432cdb54edff43d4af173e532844a0a0ea
[[ -d "$deps/hdf4-hdf4.3.1" ]] || tar -xzf "$deps/hdf4.tar.gz" -C "$deps"
[[ -d "$deps/gdal-3.13.3" ]] || tar -xzf "$deps/gdal.tar.gz" -C "$deps"
cmake -S "$deps/hdf4-hdf4.3.1" -B "$deps/hdf4-build" \
    -DCMAKE_INSTALL_PREFIX="$deps/hdf4" -DBUILD_SHARED_LIBS=ON \
    -DBUILD_TESTING=OFF -DHDF4_BUILD_TOOLS=OFF -DHDF4_BUILD_EXAMPLES=OFF \
    -DHDF4_ENABLE_NETCDF=OFF -DHDF4_ENABLE_SZIP_SUPPORT=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5
cmake --build "$deps/hdf4-build" --parallel 6
cmake --install "$deps/hdf4-build"
cmake -S "$project_dir/cmake/hdf4-plugin" -B "$deps/hdf4-driver" -G Ninja \
    -DGDAL_SOURCE_DIR="$deps/gdal-3.13.3" -DHDF4_ROOT="$deps/hdf4"
cmake --build "$deps/hdf4-driver" --parallel 6
echo "Configure GeoReader with -DGEOREADER_GDAL_PLUGIN_DIR=$deps/hdf4-driver"
