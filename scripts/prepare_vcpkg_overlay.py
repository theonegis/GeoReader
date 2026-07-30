#!/usr/bin/env python3

"""Prepare the pinned vcpkg Mapnik port for the version used by GeoReader."""

from __future__ import annotations

import argparse
import json
import re
import shutil
from pathlib import Path

MAPNIK_VERSION = "4.3.0"
MAPNIK_SHA512 = (
    "fc76b1bddee8f9828db0d2ea6239caae14f8539ce95cd28d42662f207adf01d0"
    "7c1d9de4c7db86e559a22ee30b5a5713b39ee9d28b0178ddfb17a87f19237877"
)

# Mapnik 4.3 增加了若干默认开启的可选组件。GeoReader 只需要 GDAL/OGR、
# GeoJSON、Raster 和 Shape，显式关闭其余新组件可避免引入无关依赖。
MAPNIK_43_OPTIONS = (
    "-DUSE_AVIF=OFF",
    "-DUSE_PLUGIN_INPUT_GDAL_OGR=ON",
    "-DUSE_PLUGIN_INPUT_POSTGIS_PGRASTER=OFF",
    "-DUSE_PLUGIN_INPUT_TILES=OFF",
    "-DUSE_PLUGIN_INPUT_TILES_SSL=OFF",
)


def prepare_overlay(vcpkg_root: Path, output_root: Path) -> Path:
    source_port = vcpkg_root.resolve() / "ports" / "mapnik"
    if not (source_port / "portfile.cmake").is_file():
        raise FileNotFoundError(f"Mapnik vcpkg port was not found: {source_port}")

    destination = output_root.resolve() / "mapnik"
    if destination.exists():
        shutil.rmtree(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_port, destination)

    manifest_path = destination / "vcpkg.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["version"] = MAPNIK_VERSION
    manifest.pop("version-string", None)
    manifest.pop("port-version", None)
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )

    portfile_path = destination / "portfile.cmake"
    portfile = portfile_path.read_text(encoding="utf-8")
    portfile, replacement_count = re.subn(
        r"(?m)^(\s*SHA512\s+)[0-9a-fA-F]+$",
        rf"\g<1>{MAPNIK_SHA512}",
        portfile,
        count=1,
    )
    if replacement_count != 1:
        raise RuntimeError("Could not replace the Mapnik source SHA-512")

    feature_marker = "        ${FEATURE_OPTIONS}\n"
    if feature_marker not in portfile:
        raise RuntimeError("Could not locate Mapnik vcpkg CMake feature options")
    if MAPNIK_43_OPTIONS[0] not in portfile:
        extra_options = "".join(f"        {option}\n" for option in MAPNIK_43_OPTIONS)
        portfile = portfile.replace(
            feature_marker,
            feature_marker + extra_options,
            1,
        )
    portfile_path.write_text(portfile, encoding="utf-8")

    return destination


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Create GeoReader's Mapnik 4.3 vcpkg overlay port."
    )
    parser.add_argument("--vcpkg-root", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()

    destination = prepare_overlay(arguments.vcpkg_root, arguments.output)
    print(f"Prepared Mapnik {MAPNIK_VERSION} overlay: {destination}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
