from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))

from prepare_vcpkg_overlay import (  # noqa: E402
    MAPNIK_43_OPTIONS,
    MAPNIK_SHA512,
    MAPNIK_VERSION,
    prepare_overlay,
)


class PrepareVcpkgOverlayTests(unittest.TestCase):
    def test_prepares_mapnik_43_port(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            source_port = root / "vcpkg" / "ports" / "mapnik"
            source_port.mkdir(parents=True)
            (source_port / "vcpkg.json").write_text(
                json.dumps({"name": "mapnik", "version": "4.0.7"}),
                encoding="utf-8",
            )
            (source_port / "portfile.cmake").write_text(
                "vcpkg_from_github(\n"
                "    SHA512 deadbeef\n"
                ")\n"
                "vcpkg_cmake_configure(\n"
                "    OPTIONS\n"
                "        ${FEATURE_OPTIONS}\n"
                ")\n",
                encoding="utf-8",
            )

            destination = prepare_overlay(
                root / "vcpkg",
                root / "overlays",
            )
            manifest = json.loads(
                (destination / "vcpkg.json").read_text(encoding="utf-8")
            )
            portfile = (destination / "portfile.cmake").read_text(
                encoding="utf-8"
            )

            self.assertEqual(manifest["version"], MAPNIK_VERSION)
            self.assertIn(MAPNIK_SHA512, portfile)
            for option in MAPNIK_43_OPTIONS:
                self.assertIn(option, portfile)


if __name__ == "__main__":
    unittest.main()
