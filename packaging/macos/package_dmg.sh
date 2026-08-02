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

# macdeployqt 会从完整 Qt SDK 中带入未被 GeoReader 使用的数据库驱动。
# 这些插件依赖 Runner 上不存在的 ODBC、PostgreSQL 或 Mimer 客户端；项目
# 没有链接 Qt6::Sql，因此从临时 APP 中移除它们既安全也能缩小安装包。
unused_sql_plugins="$app_path/Contents/PlugIns/sqldrivers"
if [[ -d "$unused_sql_plugins" ]]; then
    rm -rf -- "$unused_sql_plugins"
fi

# Homebrew's split Qt formula can leave native plug-in directories out of
# macdeployqt's initial scan. Copy the runtime categories GeoReader needs,
# dereferencing Homebrew symlinks, then let macdeployqt rewrite their Qt paths.
qt_plugin_root="$(qtpaths --plugin-dir)"
qt_runtime_plugins=(
    "platforms/libqcocoa.dylib"
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

# CI 的发布包使用 vcpkg 为当前架构重新编译 GDAL、Mapnik 和 ICU。
# 此时只允许从 vcpkg 搜索依赖，避免 dylibbundler 回退到 Runner 上更高
# deployment target 的 Homebrew bottle；普通本地打包仍使用 Homebrew 路径。
if [[ -n "${GEOREADER_VCPKG_RUNTIME_ROOT:-}" ]]; then
    vcpkg_runtime_root="$(
        cd "$GEOREADER_VCPKG_RUNTIME_ROOT"
        pwd -P
    )"
    if [[ ! -d "$vcpkg_runtime_root/lib" ]]; then
        echo "vcpkg runtime lib directory is missing: $vcpkg_runtime_root/lib" >&2
        exit 1
    fi
    bundle_arguments+=(-s "$vcpkg_runtime_root/lib")
else
    for formula in mapnik gdal icu4c@78 icu4c; do
        if formula_prefix="$(brew --prefix "$formula" 2>/dev/null)"; then
            bundle_arguments+=(-s "$formula_prefix/lib")
        fi
    done
fi

bundle_log="$output_dir/dylibbundler-${architecture}.log"
if ! dylibbundler "${bundle_arguments[@]}" >"$bundle_log" 2>&1; then
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
    canonical_name="$dependency_name"
    for candidate in \
        "$frameworks_dir/${dependency_name%.dylib}".*.dylib; do
        [[ -f "$candidate" ]] || continue
        candidate_name="$(basename "$candidate")"
        if [[ ${#candidate_name} -gt ${#canonical_name} ]]; then
            canonical_name="$candidate_name"
        fi
    done
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

version_exceeds_macos_12()
{
    local version="$1"
    awk -v version="$version" 'BEGIN {
        split(version, parts, ".")
        major = parts[1] + 0
        minor = parts[2] + 0
        exit ! (major > 12 || (major == 12 && minor > 0))
    }'
}

list_macho_load_dependencies()
{
    local binary="$1"

    # otool -L 同时显示 LC_ID_DYLIB（动态库自己的安装名）和真正的加载项，
    # 不能用于依赖审计。这里只读取 dyld 实际会加载的命令，避免把插件自身
    # 位于 APP 内的安装名误报为外部依赖。
    otool -l "$binary" | awk '
        $1 == "cmd" {
            is_load = ($2 == "LC_LOAD_DYLIB" ||
                       $2 == "LC_LOAD_WEAK_DYLIB" ||
                       $2 == "LC_REEXPORT_DYLIB" ||
                       $2 == "LC_LOAD_UPWARD_DYLIB" ||
                       $2 == "LC_LAZY_LOAD_DYLIB")
            next
        }
        is_load && $1 == "name" {
            dependency = $0
            sub(/^[[:space:]]*name[[:space:]]+/, "", dependency)
            sub(/[[:space:]]+\(offset[[:space:]][0-9]+\)$/, "", dependency)
            print dependency
            is_load = 0
        }
    '
}

plist_minimum="$(
    /usr/libexec/PlistBuddy \
        -c 'Print :LSMinimumSystemVersion' \
        "$app_path/Contents/Info.plist"
)"
if [[ "$plist_minimum" != "12.0" ]]; then
    echo "Unexpected LSMinimumSystemVersion: $plist_minimum" >&2
    exit 1
fi

# 验证整个 APP，而不只验证主程序。任意 Qt、GDAL、Mapnik 或 ICU 二进制
# 若要求 macOS 12 之后的系统，DMG 都不能标记为支持 Monterey。
audit_errors=()
while IFS= read -r -d '' binary; do
    file "$binary" | grep -q 'Mach-O' || continue

    minimum_versions="$(
        xcrun vtool -show-build "$binary" 2>/dev/null \
            | awk '$1 == "minos" { print $2 }' \
            | sort -u
    )" || minimum_versions=""
    if [[ -z "$minimum_versions" ]]; then
        audit_errors+=("Cannot determine the deployment target: $binary")
        continue
    fi
    while IFS= read -r minimum_version; do
        if version_exceeds_macos_12 "$minimum_version"; then
            audit_errors+=(
                "$binary requires macOS $minimum_version (maximum is 12.0)"
            )
        fi
    done <<< "$minimum_versions"

    if ! load_dependencies="$(
        list_macho_load_dependencies "$binary" 2>/dev/null
    )"; then
        audit_errors+=("Cannot inspect Mach-O dependencies: $binary")
        continue
    fi
    if [[ -n "$load_dependencies" ]]; then
        while IFS= read -r dependency; do
            case "$dependency" in
                @*|/System/*|/usr/lib/*)
                    ;;
                *)
                    audit_errors+=(
                        "Unbundled macOS dependency in $binary: $dependency"
                    )
                    ;;
            esac
        done <<< "$load_dependencies"
    fi
done < <(find "$app_path" -type f -print0)

if (( ${#audit_errors[@]} > 0 )); then
    printf '%s\n' "${audit_errors[@]}" >&2
    exit 1
fi

main_minimum_versions="$(
    xcrun vtool -show-build "$executable" \
        | awk '$1 == "minos" { print $2 }' \
        | sort -u
)"
if [[ "$main_minimum_versions" != "12.0" ]]; then
    echo "GeoReader executable does not target exactly macOS 12.0: " \
        "$main_minimum_versions" >&2
    exit 1
fi

echo "Verified macOS 12 compatibility for the complete app bundle"

dmg_path="$output_dir/GeoReader-macOS-${architecture}.dmg"
local_dmg_path="$work_dir/GeoReader-macOS-${architecture}.dmg"
hdiutil create \
    -volname "GeoReader" \
    -srcfolder "$app_path" \
    -ov \
    -format UDZO \
    "$local_dmg_path"
ditto "$local_dmg_path" "$dmg_path"

echo "Created $dmg_path"
