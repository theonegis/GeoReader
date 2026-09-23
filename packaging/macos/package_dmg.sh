#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 3 ]]; then
    echo "usage: package_dmg.sh <GeoReader.app> <architecture> <output-dir>" >&2
    exit 2
fi

source_app_path="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
architecture="$2"
mkdir -p "$3"
output_dir="$(cd "$3" && pwd)"
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd "$script_dir/../.." && pwd -P)"

# Perform install_name_tool-heavy work on the runner's local temporary disk.
# This also avoids transient rename failures when a checkout lives in a synced
# folder such as Synology Drive.
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/georeader-package.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
app_path="$work_dir/GeoReader.app"
ditto "$source_app_path" "$app_path"

frameworks_dir="$app_path/Contents/Frameworks"
executable="$app_path/Contents/MacOS/GeoReader"

if [[ ! -x "$executable" ]]; then
    echo "GeoReader executable was not found in $app_path" >&2
    exit 1
fi

mkdir -p "$frameworks_dir"

# Deploy Qt frameworks and QML modules on the runner's local temporary disk.
# Running macdeployqt inside a synced checkout makes thousands of small bundle
# rewrites unnecessarily slow and can trigger transient rename failures.
qt_bin_dir="$(qtpaths --binaries-dir)"
macdeployqt="$qt_bin_dir/macdeployqt"
if [[ ! -x "$macdeployqt" ]]; then
    echo "macdeployqt was not found at $macdeployqt" >&2
    exit 1
fi
"$macdeployqt" "$app_path" \
    "-qmldir=$project_root/qml" \
    "-always-overwrite"

# Homebrew's split Qt formula can leave native plug-in directories out of
# macdeployqt's initial scan. Copy the runtime categories GeoReader needs,
# dereferencing Homebrew symlinks, then let macdeployqt rewrite their Qt paths.
qt_plugin_root="$(qtpaths --plugin-dir)"
qt_runtime_plugins=(
    "platforms/libqcocoa.dylib"
    "platforms/libqoffscreen.dylib"
    "tls/libqsecuretransportbackend.dylib"
    "networkinformation/libqapplenetworkinformation.dylib"
    "imageformats/libqgif.dylib"
    "imageformats/libqjpeg.dylib"
    "imageformats/libqtiff.dylib"
    "styles/libqmacstyle.dylib"
)
for relative_plugin in "${qt_runtime_plugins[@]}"; do
    source_plugin="$qt_plugin_root/$relative_plugin"
    [[ -e "$source_plugin" ]] || continue
    destination_plugin="$app_path/Contents/PlugIns/$relative_plugin"
    mkdir -p "$(dirname "$destination_plugin")"
    ditto "$(realpath "$source_plugin")" "$destination_plugin"
done

# Rewrite the newly copied plug-ins' Qt framework references directly so the
# deployed QML modules do not need to be scanned a second time.
for relative_plugin in "${qt_runtime_plugins[@]}"; do
    plugin="$app_path/Contents/PlugIns/$relative_plugin"
    [[ -f "$plugin" ]] || continue
    while IFS= read -r dependency; do
        if [[ "$dependency" =~ /([^/]+)\.framework/Versions/[^/]+/([^/]+)$ ]]; then
            framework_name="${BASH_REMATCH[1]}"
            framework_binary="${BASH_REMATCH[2]}"
            bundled_framework="$frameworks_dir/$framework_name.framework/Versions/A/$framework_binary"
            if [[ -f "$bundled_framework" ]]; then
                install_name_tool -change "$dependency" \
                    "@executable_path/../Frameworks/$framework_name.framework/Versions/A/$framework_binary" \
                    "$plugin"
            fi
        fi
    done < <(otool -L "$plugin" | awk 'NR > 1 { print $1 }')
done

bundle_arguments=(
    -cd
    -of
    -b
    -d "$frameworks_dir"
    -p @rpath/
    -x "$executable"
)

while IFS= read -r plugin; do
    bundle_arguments+=(-x "$plugin")
done < <(find "$app_path/Contents/PlugIns/mapnik/input" \
    -type f -name "*.input" -print | sort)

for relative_plugin in "${qt_runtime_plugins[@]}"; do
    plugin="$app_path/Contents/PlugIns/$relative_plugin"
    [[ -f "$plugin" ]] || continue
    bundle_arguments+=(-x "$plugin")
done

# Scientific format drivers must be processed as well: HDF4 brings its own
# libmfhdf/libhdf, which are not direct dependencies of libgdal.
while IFS= read -r plugin; do
    bundle_arguments+=(-x "$plugin")
done < <(find "$app_path/Contents/PlugIns/gdal" -type f -name '*.dylib' -print | sort)


for formula in mapnik gdal icu4c@78 icu4c; do
    if formula_prefix="$(brew --prefix "$formula" 2>/dev/null)"; then
        bundle_arguments+=(-s "$formula_prefix/lib")
    fi
done

# Locally built HDF4 libraries use @rpath install names. Search their build
# prefix explicitly; this directory is only needed on the packaging machine.
for hdf4_prefix in "${HDF4_ROOT:-}" "$project_root/.test-deps/hdf4" "$project_root/.test-deps/hdf4-driver"; do
    [[ -n "$hdf4_prefix" && -d "$hdf4_prefix/lib" ]] || continue
    bundle_arguments+=(-s "$hdf4_prefix/lib")
done
bundle_log="$output_dir/dylibbundler-${architecture}.log"
if ! dylibbundler "${bundle_arguments[@]}" <<< quit >"$bundle_log" 2>&1; then
    tail -n 200 "$bundle_log" >&2
    exit 1
fi

# dylibbundler only needs to process the native executable and Mapnik plug-ins.
# Preserve the QML deployment generated by macdeployqt verbatim: some
# dylibbundler releases prune app-bundle directories they do not inspect.
for deployed_qt_directory in \
    "Contents/PlugIns/quick" \
    "Contents/Resources/qml"; do
    source_directory="$source_app_path/$deployed_qt_directory"
    [[ -d "$source_directory" ]] || continue
    ditto "$source_directory" "$app_path/$deployed_qt_directory"
done

# Runtime data is resolved relative to the app. Core PROJ definitions are
# bundled; optional, multi-gigabyte regional datum grids are not required by
# the WGS84/Web Mercator viewer and can be supplied by a future grid manager.
mkdir -p "$app_path/Contents/Resources/gdal" "$app_path/Contents/Resources/proj"
ditto "$(brew --prefix gdal)/share/gdal" "$app_path/Contents/Resources/gdal"
for proj_file in proj.db proj.ini; do
    cp "$(brew --prefix proj)/share/proj/$proj_file" "$app_path/Contents/Resources/proj/"
done

# macdeployqt may refer to a compatibility-name dylib while dylibbundler
# copies only the fully versioned file. Create the missing aliases after all
# dependencies have been collected.
while IFS= read -r dependency_name; do
    dependency_path="$frameworks_dir/$dependency_name"
    [[ -e "$dependency_path" ]] && continue

    for candidate in \
        "$frameworks_dir/${dependency_name%.dylib}"*.dylib; do
        [[ -f "$candidate" ]] || continue
        ln -s "$(basename "$candidate")" "$dependency_path"
        break
    done

    if [[ ! -e "$dependency_path" ]]; then
        echo "Unresolved bundled dependency: $dependency_name" >&2
        exit 1
    fi
done < <(
    while IFS= read -r -d '' binary; do
        otool -L "$binary" 2>/dev/null \
            | awk 'NR > 1 { print $1 }'
    done < <(find "$app_path" -type f -print0) \
        | awk -F/ '/^@(rpath|loader_path|executable_path).*\.dylib$/ {
            print $NF
        }' \
        | sort -u
)

install_name_tool -add_rpath "@executable_path/../Frameworks" \
    "$executable" 2>/dev/null || true

# macdeployqt may link the executable through a compatibility symlink while
# dylibbundler links Mapnik plug-ins to the fully versioned file. dyld can then
# load two copies of GDAL/Mapnik, which splits process-global registries such as
# GDAL's /vsimem filesystem. Normalize direct dylib dependencies to the same
# canonical bundled filenames used by the plug-ins.
while IFS= read -r dependency; do
    dependency_name="$(basename "$dependency")"
    compatibility_path="$frameworks_dir/$dependency_name"
    [[ -L "$compatibility_path" ]] || continue
    canonical_name="$(basename "$(realpath "$compatibility_path")")"
    [[ "$canonical_name" != "$dependency_name" ]] || continue
    install_name_tool -change "$dependency" \
        "@rpath/$canonical_name" "$executable"
done < <(
    otool -L "$executable" | awk 'NR > 1 { print $1 }'
)

while IFS= read -r plugin; do
    install_name_tool -add_rpath "@loader_path/../../../Frameworks" \
        "$plugin" 2>/dev/null || true
done < <(find "$app_path/Contents/PlugIns/mapnik/input" \
    -type f -name "*.input" -print | sort)

# Some Homebrew dependency chains carry the same LC_RPATH more than once.
# Recent dyld versions reject a library with duplicate LC_RPATH commands.
# dylibbundler-created dylibs live directly in Frameworks; Qt framework
# binaries are deliberately left untouched.
while IFS= read -r -d '' binary; do
    [[ "$binary" == *".framework/"* ]] && continue
    file "$binary" | grep -q "Mach-O" || continue

    modified=false
    while IFS= read -r rpath; do
        case "$rpath" in
            /opt/*|/usr/local/*|/Users/*)
                install_name_tool -delete_rpath "$rpath" "$binary"
                modified=true ;;
        esac
    done < <(otool -l "$binary" | awk '/LC_RPATH/ { getline; getline; print $2 }')
    while duplicate_rpath="$(
        otool -l "$binary" \
            | awk '/LC_RPATH/ { getline; getline; print $2 }' \
            | sort \
            | uniq -d \
            | head -n 1
    )" && [[ -n "$duplicate_rpath" ]]; do
        install_name_tool -delete_rpath "$duplicate_rpath" "$binary"
        modified=true
    done

    if [[ "$modified" == true ]]; then
        codesign --force --sign - "$binary"
    fi
done < <(find "$app_path" -type f -print0)

# The rpath edits above invalidate only the main executable and Mapnik input
# plug-ins. dylibbundler already signs the dylibs that it copies. Avoid
# re-signing individual Qt framework binaries because a framework must remain
# signed as a bundle.
codesign --force --sign - "$executable"
while IFS= read -r plugin; do
    codesign --force --sign - "$plugin"
done < <(find "$app_path/Contents/PlugIns/mapnik/input" \
    -type f -name "*.input" -print | sort)

# Normalize Qt framework references left by split Homebrew Qt deployments.
python3 "$project_root/scripts/relocate_macos_bundle.py" "$app_path"

# CI artifacts are ad-hoc signed; a Developer ID can replace this later.
# --deep refreshes the signatures of the QML plug-ins restored above.
codesign --force --deep --sign - "$app_path"
codesign --verify --deep --strict "$app_path"

required_runtime_files=(
    "Contents/PlugIns/platforms/libqcocoa.dylib"
    "Contents/PlugIns/quick/libqtquickcontrols2plugin.dylib"
    "Contents/Resources/qml/QtQuick/Controls/qmldir"
)
for required_runtime_file in "${required_runtime_files[@]}"; do
    if [[ ! -e "$app_path/$required_runtime_file" ]]; then
        echo "Required Qt runtime file is missing: $required_runtime_file" >&2
        exit 1
    fi
done

# Refuse to deliver a bundle that still depends on the build machine.
python3 "$project_root/scripts/audit_macos_bundle.py" "$app_path"
QT_QPA_PLATFORM=offscreen "$executable" --runtime-check \
    "$project_root/tests/data/netcdf4.nc" "$project_root/tests/data/scientific.h5" \
    "$project_root/tests/data/scientific.hdf" > "$output_dir/runtime-check.json"
# Preserve the complete runnable app as well as the installer image.
ditto "$app_path" "$output_dir/GeoReader.app"
dmg_path="$output_dir/GeoReader-macOS-${architecture}.dmg"
hdiutil create \
    -volname "GeoReader" \
    -srcfolder "$app_path" \
    -ov \
    -format UDZO \
    "$dmg_path"

echo "Created $dmg_path"
