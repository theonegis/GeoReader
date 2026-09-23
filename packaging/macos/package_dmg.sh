#!/usr/bin/env bash
set -euo pipefail
if [[ $# -ne 3 ]]; then
    echo "usage: package_dmg.sh <GeoReader.app> <architecture> <output-dir>" >&2
    exit 2
fi
project="$(cd "$(dirname "$0")/../.." && pwd)"
source_app_path="$(cd "$(dirname "$1")" && pwd)/$(basename "$1")"
architecture="$2"
mkdir -p "$3"
output_dir="$(cd "$3" && pwd)"
work_dir="$(mktemp -d "${TMPDIR:-/tmp}/georeader-package.XXXXXX")"
trap 'rm -rf "$work_dir"' EXIT
app_path="$work_dir/GeoReader.app"
# Only the executable and explicitly used plugins seed the dependency closure.
python3 "$project/scripts/bundle_macos.py" "$source_app_path" "$app_path" \
    --search "$project/.test-deps/hdf4/lib"
python3 "$project/scripts/audit_macos_bundle.py" "$app_path"
"$app_path/Contents/MacOS/GeoReader" --runtime-check > "$output_dir/runtime-check.json"
dmg_path="$output_dir/GeoReader-macOS-${architecture}.dmg"
local_dmg_path="$work_dir/GeoReader-macOS-${architecture}.dmg"
create_dmg_with_retries()
{
    local attempt
    local hdiutil_status=1
    local max_attempts=4

    for ((attempt = 1; attempt <= max_attempts; attempt++)); do
        rm -f -- "$local_dmg_path"
        if hdiutil create \
            -volname "GeoReader" \
            -srcfolder "$app_path" \
            -ov \
            -format UDZO \
            "$local_dmg_path"; then
            return 0
        else
            hdiutil_status=$?
        fi

        if (( attempt == max_attempts )); then
            break
        fi

        echo "hdiutil create failed (attempt $attempt/$max_attempts); retrying" >&2
        sync
        sleep $((attempt * 5))
    done

    return "$hdiutil_status"
}

create_dmg_with_retries
hdiutil verify "$local_dmg_path"
ditto "$local_dmg_path" "$dmg_path"
ditto "$app_path" "$output_dir/GeoReader.app"
cp "$work_dir/GeoReader.dependencies.json" "$output_dir/GeoReader.dependencies.json"
echo "Created $dmg_path"
