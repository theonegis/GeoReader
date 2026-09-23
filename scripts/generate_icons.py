#!/usr/bin/env python3
"""Compatibility entrypoint for the platform-aware Qt SVG icon generator."""
import subprocess
from pathlib import Path
root = Path(__file__).resolve().parent.parent
subprocess.run(["cmake", "-S", str(root), "-B", str(root / "build-icons"),
                "-DGEOREADER_BUILD_ICON_TOOLS=ON"], check=True)
subprocess.run(["cmake", "--build", str(root / "build-icons"), "--target", "GeoReaderIconTool"], check=True)
subprocess.run([str(root / "build-icons/GeoReaderIconTool"), str(root)], check=True)
