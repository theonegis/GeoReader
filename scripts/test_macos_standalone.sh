#!/usr/bin/env bash
# Run from the checkout so the test harness can locate fixtures and save evidence.
set -euo pipefail
project="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
app="${1:-$project/dist/scientific/GeoReader.app}"
app="$(cd "$(dirname "$app")" && pwd)/$(basename "$app")"
cd "$project"
mkdir -p tests/output
sandbox=(/usr/bin/sandbox-exec -D "PROJECT_DEPS=$project/.test-deps" -D "PROJECT_BUILD=$project/build-scientific" -f "$project/tests/standalone-macos.sb")
run=(/usr/bin/env -i "PATH=/usr/bin:/bin" "HOME=$HOME" "TMPDIR=${TMPDIR:-/tmp}" QT_QPA_PLATFORM=offscreen QT_QUICK_BACKEND=software GDAL_DRIVER_PATH=/nonexistent GDAL_DATA=/nonexistent PROJ_DATA=/nonexistent "$app/Contents/MacOS/GeoReader")
"${sandbox[@]}" "${run[@]}" --runtime-check "$project/tests/data/netcdf4.nc" "$project/tests/data/scientific.h5" "$project/tests/data/scientific.hdf" > tests/output/standalone-runtime.json
"${sandbox[@]}" "${run[@]}" --scientific-ui-test > tests/output/standalone-ui.log 2>&1
cat tests/output/standalone-runtime.json
tail -n 5 tests/output/standalone-ui.log
