#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd "$script_dir/.." && pwd -P)"

build_type="Release"
build_dir="$project_root/build"
clean_first=false
create_package=false
package_format="auto"
parallel_jobs=""
cmake_extra_args=()
has_cmake_extra_args=false

usage()
{
    cat <<'EOF'
Usage: ./scripts/build.sh [options] [-- <extra CMake arguments>]

Options:
  --type <type>       CMake build type (default: Release)
  --build-dir <path>  Build directory inside the project (default: build)
  --jobs <count>      Maximum number of parallel build jobs
  --clean-first       Remove the selected build directory before configuring
  --package           Create a platform-appropriate package
  --package-format F  Package format: auto, all, dmg, deb, rpm, or tgz
  -h, --help          Show this help

Examples:
  ./scripts/build.sh
  ./scripts/build.sh --type Debug --jobs 8
  ./scripts/build.sh --clean-first --package
  ./scripts/build.sh --package --package-format rpm
  ./scripts/build.sh -- -DMAPNIK_INPUT_PLUGIN_DIR=/usr/lib/mapnik/input
EOF
}

resolve_project_path()
{
    local requested_path="$1"
    local candidate_path
    local parent_directory
    local leaf_name

    if [[ "$requested_path" == /* ]]; then
        candidate_path="$requested_path"
    else
        candidate_path="$project_root/$requested_path"
    fi

    if [[ -d "$candidate_path" ]]; then
        (
            cd "$candidate_path"
            pwd -P
        )
        return
    fi

    parent_directory="$(dirname "$candidate_path")"
    leaf_name="$(basename "$candidate_path")"
    if [[ "$leaf_name" == "." || "$leaf_name" == ".." ]]; then
        echo "Invalid build directory: $requested_path" >&2
        exit 2
    fi
    if [[ ! -d "$parent_directory" ]]; then
        echo "The build directory parent does not exist: $parent_directory" >&2
        exit 2
    fi

    parent_directory="$(cd "$parent_directory" && pwd -P)"
    printf '%s/%s\n' "$parent_directory" "$leaf_name"
}

require_value()
{
    local option="$1"
    local value="${2:-}"
    if [[ -z "$value" ]]; then
        echo "$option requires a value." >&2
        usage >&2
        exit 2
    fi
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --type)
            require_value "$1" "${2:-}"
            build_type="$2"
            shift 2
            ;;
        --build-dir)
            require_value "$1" "${2:-}"
            build_dir="$(resolve_project_path "$2")"
            shift 2
            ;;
        --jobs)
            require_value "$1" "${2:-}"
            if [[ ! "$2" =~ ^[1-9][0-9]*$ ]]; then
                echo "--jobs must be a positive integer." >&2
                exit 2
            fi
            parallel_jobs="$2"
            shift 2
            ;;
        --clean-first)
            clean_first=true
            shift
            ;;
        --package)
            create_package=true
            shift
            ;;
        --package-format)
            require_value "$1" "${2:-}"
            case "$2" in
                auto|all|dmg|deb|rpm|tgz)
                    package_format="$2"
                    ;;
                *)
                    echo "Unsupported package format: $2" >&2
                    exit 2
                    ;;
            esac
            create_package=true
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            cmake_extra_args=("$@")
            has_cmake_extra_args=true
            break
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

case "$build_dir" in
    "$project_root"|"$project_root/")
        echo "The project root cannot be used as the build directory." >&2
        exit 2
        ;;
    "$project_root"/*)
        ;;
    *)
        echo "The build directory must be inside $project_root." >&2
        exit 2
        ;;
esac

for required_command in cmake ninja; do
    if ! command -v "$required_command" >/dev/null 2>&1; then
        echo "Required command was not found: $required_command" >&2
        exit 1
    fi
done

if [[ "$clean_first" == true ]]; then
    "$script_dir/clean.sh" --build-dir "$build_dir"
fi

cmake_arguments=(
    -S "$project_root"
    -B "$build_dir"
    -G Ninja
    "-DCMAKE_BUILD_TYPE=$build_type"
)

if [[ "$(uname -s)" == "Darwin" ]] \
   && [[ -z "${CMAKE_PREFIX_PATH:-}" ]] \
   && command -v brew >/dev/null 2>&1; then
    qt_prefix="$(brew --prefix qt 2>/dev/null || true)"
    if [[ -n "$qt_prefix" ]]; then
        cmake_arguments+=("-DCMAKE_PREFIX_PATH=$qt_prefix")
    fi
fi

if [[ "$has_cmake_extra_args" == true ]]; then
    cmake "${cmake_arguments[@]}" "${cmake_extra_args[@]}"
else
    cmake "${cmake_arguments[@]}"
fi

build_arguments=(--build "$build_dir" --parallel)
if [[ -n "$parallel_jobs" ]]; then
    build_arguments+=("$parallel_jobs")
fi
cmake "${build_arguments[@]}"

platform_name="$(uname -s)"
architecture="$(uname -m)"
runtime_output_dir="$project_root/dist/runtime/${platform_name}-${architecture}"
mkdir -p "$runtime_output_dir"

case "$(uname -s)" in
    Darwin)
        executable_path="$build_dir/GeoReader.app/Contents/MacOS/GeoReader"
        runtime_output="$runtime_output_dir/GeoReader.app"
        rm -rf -- "$runtime_output"
        ditto "$build_dir/GeoReader.app" "$runtime_output"
        ;;
    *)
        executable_path="$build_dir/GeoReader"
        runtime_output="$runtime_output_dir/GeoReader"
        cp -p "$executable_path" "$runtime_output"
        ;;
esac

echo "Build completed: $executable_path"
echo "Runnable output preserved at: $runtime_output"

if [[ "$create_package" != true ]]; then
    exit 0
fi

case "$(uname -s)" in
    Darwin)
        if [[ "$package_format" != "auto" \
              && "$package_format" != "all" \
              && "$package_format" != "dmg" ]]; then
            echo "macOS supports package formats: auto, all, dmg." >&2
            exit 2
        fi
        if ! command -v dylibbundler >/dev/null 2>&1; then
            echo "Packaging requires dylibbundler: brew install dylibbundler" >&2
            exit 1
        fi

        stage_dir="$project_root/stage"
        "$script_dir/clean.sh" --stage-only
        cmake --install "$build_dir" --prefix "$stage_dir"
        bash "$project_root/packaging/macos/package_dmg.sh" \
            "$stage_dir/GeoReader.app" "$architecture" "$project_root/dist"
        echo "Package completed: $project_root/dist"
        ;;
    Linux)
        linux_distribution="unknown"
        if [[ -r /etc/os-release ]]; then
            linux_distribution="$(
                sed -n 's/^ID=//p' /etc/os-release \
                    | head -n 1 \
                    | tr -d '"'
            )"
        fi

        generators=()
        case "$package_format" in
            auto)
                case "$linux_distribution" in
                    ubuntu|debian|linuxmint|pop)
                        generators=(DEB)
                        ;;
                    fedora|rhel|centos|rocky|almalinux)
                        generators=(RPM)
                        ;;
                    arch|cachyos|manjaro|endeavouros)
                        generators=(TGZ)
                        ;;
                    *)
                        if command -v dpkg-deb >/dev/null 2>&1; then
                            generators=(DEB)
                        elif command -v rpmbuild >/dev/null 2>&1; then
                            generators=(RPM)
                        else
                            generators=(TGZ)
                        fi
                        ;;
                esac
                ;;
            all)
                generators=(DEB RPM TGZ)
                ;;
            deb)
                generators=(DEB)
                ;;
            rpm)
                generators=(RPM)
                ;;
            tgz)
                generators=(TGZ)
                ;;
            dmg)
                echo "DMG packages can only be created on macOS." >&2
                exit 2
                ;;
        esac

        for generator in "${generators[@]}"; do
            if [[ "$generator" == "DEB" ]] \
               && ! command -v dpkg-deb >/dev/null 2>&1; then
                echo "DEB packaging requires dpkg-deb." >&2
                exit 1
            fi
            if [[ "$generator" == "RPM" ]] \
               && ! command -v rpmbuild >/dev/null 2>&1; then
                echo "RPM packaging requires rpmbuild." >&2
                exit 1
            fi
            cpack --config "$build_dir/CPackConfig.cmake" \
                -G "$generator" -B "$project_root/dist"
        done
        echo "Linux packages completed (${generators[*]}): $project_root/dist"
        ;;
    *)
        echo "--package is supported by this script on macOS and Linux." >&2
        echo "Use the documented PowerShell/CPack commands on Windows." >&2
        exit 1
        ;;
esac
