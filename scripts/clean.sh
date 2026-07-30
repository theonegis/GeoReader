#!/usr/bin/env bash

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
project_root="$(cd "$script_dir/.." && pwd -P)"

remove_packages=false
dry_run=false
stage_only=false
custom_build_dir=""

usage()
{
    cat <<'EOF'
Usage: ./scripts/clean.sh [options]

By default, this removes build/stage directories and temporary check outputs,
while preserving runnable applications and installers under dist directories.

Options:
  --build-dir <path>  Remove only the selected build directory
  --stage-only        Remove only the default stage directory
  --all               Also remove dist directories and generated installers
  --dry-run           Print the targets without deleting them
  -h, --help          Show this help

Examples:
  ./scripts/clean.sh --dry-run
  ./scripts/clean.sh
  ./scripts/clean.sh --all
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

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-dir)
            if [[ -z "${2:-}" ]]; then
                echo "--build-dir requires a value." >&2
                exit 2
            fi
            custom_build_dir="$(resolve_project_path "$2")"
            shift 2
            ;;
        --stage-only)
            stage_only=true
            shift
            ;;
        --all)
            remove_packages=true
            shift
            ;;
        --dry-run)
            dry_run=true
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -n "$custom_build_dir" && "$stage_only" == true ]]; then
    echo "--build-dir and --stage-only cannot be used together." >&2
    exit 2
fi
if [[ "$remove_packages" == true \
      && ( -n "$custom_build_dir" || "$stage_only" == true ) ]]; then
    echo "--all cannot be combined with --build-dir or --stage-only." >&2
    exit 2
fi

targets=()
build_targets=()

append_if_present()
{
    local candidate="$1"
    if [[ -e "$candidate" ]]; then
        targets+=("$candidate")
    fi
}

if [[ -n "$custom_build_dir" ]]; then
    case "$custom_build_dir" in
        "$project_root"|"$project_root/")
            echo "Refusing to remove the project root." >&2
            exit 2
            ;;
        "$project_root"/*)
            append_if_present "$custom_build_dir"
            [[ -e "$custom_build_dir" ]] \
                && build_targets+=("$custom_build_dir")
            ;;
        *)
            echo "The build directory must be inside $project_root." >&2
            exit 2
            ;;
    esac
elif [[ "$stage_only" == true ]]; then
    append_if_present "$project_root/stage"
else
    append_if_present "$project_root/build"
    [[ -e "$project_root/build" ]] \
        && build_targets+=("$project_root/build")
    append_if_present "$project_root/stage"

    for candidate in \
        "$project_root"/build-* \
        "$project_root"/stage-* \
        "$project_root"/dist-check* \
        "$project_root"/dist-*check*; do
        append_if_present "$candidate"
        if [[ -e "$candidate" && "$(basename "$candidate")" == build-* ]]; then
            build_targets+=("$candidate")
        fi
    done

    if [[ "$remove_packages" == true ]]; then
        for candidate in "$project_root"/dist "$project_root"/dist-*; do
            append_if_present "$candidate"
        done
    fi
fi

preserve_runnable()
{
    local source_build_dir="$1"
    local platform_name
    local architecture
    local output_directory

    platform_name="$(uname -s)"
    architecture="$(uname -m)"
    output_directory="$project_root/dist/runtime/${platform_name}-${architecture}"

    if [[ -x "$source_build_dir/GeoReader.app/Contents/MacOS/GeoReader" ]]; then
        mkdir -p "$output_directory"
        rm -rf -- "$output_directory/GeoReader.app"
        ditto "$source_build_dir/GeoReader.app" \
            "$output_directory/GeoReader.app"
        echo "Preserved runnable app: $output_directory/GeoReader.app"
        return 0
    elif [[ -x "$source_build_dir/GeoReader" ]]; then
        mkdir -p "$output_directory"
        cp -p "$source_build_dir/GeoReader" "$output_directory/GeoReader"
        echo "Preserved runnable executable: $output_directory/GeoReader"
        return 0
    fi
    return 1
}

if [[ ${#targets[@]} -eq 0 ]]; then
    echo "Nothing to clean."
    exit 0
fi

printf 'Cleanup targets:\n'
printf '  %s\n' "${targets[@]}"

if [[ "$dry_run" == true ]]; then
    if [[ "$remove_packages" == false && ${#build_targets[@]} -gt 0 ]]; then
        echo "Runnable outputs found in build directories would be preserved under dist/runtime."
    fi
    echo "Dry run only; nothing was removed."
    exit 0
fi

if [[ "$remove_packages" == false && ${#build_targets[@]} -gt 0 ]]; then
    for build_target in "${build_targets[@]}"; do
        if preserve_runnable "$build_target"; then
            break
        fi
    done
fi

for target in "${targets[@]}"; do
    rm -rf -- "$target"
done

if [[ "$remove_packages" == true ]]; then
    echo "Build files and generated installers were removed."
else
    find "$project_root" -type f \
        \( -name 'dylibbundler-*.log' -o -name '.DS_Store' \) \
        -delete
    echo "Temporary files were removed; runnable outputs and installers were preserved."
fi
