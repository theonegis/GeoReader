from __future__ import annotations

import importlib.util
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location(
    "build_minimal_gdal",
    PROJECT_ROOT / "scripts" / "build_minimal_gdal.py",
)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(MODULE)


class MinimalGdalVersionTests(unittest.TestCase):
    def test_release_name_in_header(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            header = prefix / "include" / "gdal_version.h"
            header.parent.mkdir()
            header.write_text(
                '#define GDAL_RELEASE_NAME     "3.12.4"\n',
                encoding="utf-8",
            )
            self.assertEqual(MODULE.detect_gdal_version(prefix), "3.12.4")

    def test_numeric_header_without_release_name(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            header = prefix / "include" / "gdal_version.h"
            header.parent.mkdir()
            header.write_text(
                "\n".join(
                    (
                        "#define GDAL_VERSION_MAJOR    3",
                        "#define GDAL_VERSION_MINOR    12",
                        "#define GDAL_VERSION_REV      4",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(MODULE.detect_gdal_version(prefix), "3.12.4")

    def test_cmake_package_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            cmake = prefix / "share" / "gdal" / "GDALConfigVersion.cmake"
            cmake.parent.mkdir(parents=True)
            cmake.write_text(
                'set(PACKAGE_VERSION "3.12.4")\n',
                encoding="utf-8",
            )
            self.assertEqual(MODULE.detect_gdal_version(prefix), "3.12.4")

    def test_pkgconfig_version(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            pkgconfig = prefix / "lib" / "pkgconfig" / "gdal.pc"
            pkgconfig.parent.mkdir(parents=True)
            pkgconfig.write_text(
                "Name: GDAL\nVersion: 3.12.4\n",
                encoding="utf-8",
            )
            self.assertEqual(MODULE.detect_gdal_version(prefix), "3.12.4")

    def test_vcpkg_status_matches_triplet(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            install_root = Path(directory)
            prefix = install_root / "arm64-osx-release"
            prefix.mkdir()
            status = install_root / "vcpkg" / "status"
            status.parent.mkdir()
            status.write_text(
                "\n".join(
                    (
                        "Package: gdal",
                        "Version: 3.11.0",
                        "Architecture: x64-windows-release",
                        "Status: install ok installed",
                        "",
                        "Package: gdal",
                        "Version: 3.12.4#2",
                        "Architecture: arm64-osx-release",
                        "Status: install ok installed",
                        "",
                    )
                ),
                encoding="utf-8",
            )
            self.assertEqual(MODULE.detect_gdal_version(prefix), "3.12.4")

    def test_missing_prefix_metadata_does_not_call_gdal_config(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            prefix = Path(directory)
            with self.assertRaises(RuntimeError) as caught:
                MODULE.detect_gdal_version(prefix)
            self.assertIn(str(prefix), str(caught.exception))

    def test_explicit_version_wins(self) -> None:
        self.assertEqual(MODULE.detect_gdal_version(None, "3.13.3"), "3.13.3")


if __name__ == "__main__":
    unittest.main()
