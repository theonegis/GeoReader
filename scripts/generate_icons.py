#!/usr/bin/env python3
"""Generate macOS ICNS and Windows ICO assets from the canonical SVG icon."""

from __future__ import annotations

import shutil
import struct
import subprocess
import tempfile
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
ICON_DIRECTORY = PROJECT_ROOT / "resources" / "icons"
SVG_PATH = ICON_DIRECTORY / "georeader.svg"

# ICNS PNG-backed icon chunks. Retina chunks intentionally reuse the PNG size
# of their corresponding logical point size.
ICNS_SIZES = {
    "icp4": 16,
    "icp5": 32,
    "icp6": 64,
    "ic07": 128,
    "ic08": 256,
    "ic09": 512,
    "ic10": 1024,
    "ic11": 32,
    "ic12": 64,
    "ic13": 256,
    "ic14": 512,
}


def require_tool(name: str) -> str:
    executable = shutil.which(name)
    if executable is None:
        raise SystemExit(f"Required command was not found: {name}")
    return executable


def render_png(renderer: str, size: int, destination: Path) -> None:
    subprocess.run(
        [
            renderer,
            "-w",
            str(size),
            "-h",
            str(size),
            str(SVG_PATH),
            "-o",
            str(destination),
        ],
        check=True,
    )


def write_icns(pngs: dict[int, Path], destination: Path) -> None:
    chunks: list[bytes] = []
    for chunk_type, size in ICNS_SIZES.items():
        payload = pngs[size].read_bytes()
        chunks.append(
            chunk_type.encode("ascii")
            + struct.pack(">I", len(payload) + 8)
            + payload
        )
    body = b"".join(chunks)
    destination.write_bytes(b"icns" + struct.pack(">I", len(body) + 8) + body)


def main() -> None:
    renderer = require_tool("rsvg-convert")
    imagemagick = require_tool("magick")
    with tempfile.TemporaryDirectory(prefix="georeader-icons-") as directory:
        temporary_directory = Path(directory)
        pngs: dict[int, Path] = {}
        for size in sorted(set(ICNS_SIZES.values())):
            png_path = temporary_directory / f"georeader-{size}.png"
            render_png(renderer, size, png_path)
            pngs[size] = png_path

        write_icns(pngs, ICON_DIRECTORY / "georeader.icns")
        subprocess.run(
            [
                imagemagick,
                str(pngs[1024]),
                "-define",
                "icon:auto-resize=256,128,64,48,32,16",
                str(ICON_DIRECTORY / "georeader.ico"),
            ],
            check=True,
        )

    print(f"Generated {ICON_DIRECTORY / 'georeader.icns'}")
    print(f"Generated {ICON_DIRECTORY / 'georeader.ico'}")


if __name__ == "__main__":
    main()
