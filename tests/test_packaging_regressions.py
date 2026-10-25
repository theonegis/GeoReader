from __future__ import annotations

import json
import os
from pathlib import Path
import plistlib
import re
import subprocess
import sys
import tempfile
import unittest
from unittest import mock

PROJECT_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT_ROOT / "scripts"))
import bundle_macos


class PackagingRegressionTests(unittest.TestCase):
    def run_cmake(self, source: str) -> None:
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "check.cmake"
            script.write_text(source, encoding="utf-8")
            result = subprocess.run(
                ["cmake", "-P", str(script)], capture_output=True,
                text=True, timeout=30,
            )
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_file_install_does_not_embed_cpack_staging_prefix_twice(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "libfixture.so"
            staging = root / "stage"
            source.write_bytes(b"runtime fixture")
            expected = staging / "usr/lib/georeader/libfixture.so"
            duplicated = staging / str(staging).lstrip("/") / "usr/lib/georeader/libfixture.so"

            self.run_cmake(
                f'''set(CMAKE_INSTALL_PREFIX "/usr")
                set(ENV{{DESTDIR}} "{staging}")
                set(_staging_prefix "$ENV{{DESTDIR}}${{CMAKE_INSTALL_PREFIX}}")
                set(_destination "${{CMAKE_INSTALL_PREFIX}}/lib/georeader")
                file(INSTALL "{source}" DESTINATION "${{_destination}}" TYPE FILE)
                if(NOT EXISTS "{expected}")
                    message(FATAL_ERROR "Runtime was not installed below the CPack staging root")
                endif()
                if(EXISTS "{duplicated}")
                    message(FATAL_ERROR "CPack staging root was embedded twice")
                endif()
                '''
            )

    def test_rpm_requires_is_a_single_comma_separated_string(self) -> None:
        source = (PROJECT_ROOT / "CMakeLists.txt").read_text()
        # Execute the actual packaging metadata without configuring Qt/GDAL.
        rpm_metadata = "set(CPACK_RPM_PACKAGE_LICENSE" + source.split(
            "set(CPACK_RPM_PACKAGE_LICENSE", 1
        )[1].split("endif()", 1)[0]
        self.run_cmake(rpm_metadata + '''
            if(CPACK_RPM_PACKAGE_REQUIRES MATCHES ";")
                message(FATAL_ERROR "RPM Requires contains a CMake list separator: ${CPACK_RPM_PACKAGE_REQUIRES}")
            endif()
            if(NOT CPACK_RPM_PACKAGE_REQUIRES STREQUAL "glibc >= 2.39, dbus-libs, fontconfig, libwayland-client, libwayland-cursor, libwayland-egl, libxkbcommon, libglvnd-egl, libglvnd-glx, libglvnd-opengl")
                message(FATAL_ERROR "RPM dependencies were lost")
            endif()
            if(NOT CPACK_RPM_PACKAGE_AUTOREQPROV STREQUAL "OFF")
                message(FATAL_ERROR "Bundled Qt private ABI leaked into RPM auto dependencies")
            endif()
        ''')

    def test_debian_declares_qt_opengl_runtime_dependencies(self) -> None:
        source = (PROJECT_ROOT / "CMakeLists.txt").read_text()
        for package in ("libfontconfig1", "libegl1", "libgl1", "libglx0", "libopengl0"):
            self.assertIn(package, source)

    def test_linux_prefers_pinned_runtime_and_excludes_host_openssl(self) -> None:
        source = (PROJECT_ROOT / "CMakeLists.txt").read_text()
        self.assertIn(
            '"\\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}/georeader;'
            '\\$ORIGIN/../${CMAKE_INSTALL_LIBDIR}"',
            source,
        )
        self.assertIn('"^libssl\\\\.so"', source)
        self.assertIn('"^libcrypto\\\\.so"', source)

    def test_windows_system_filter_handles_both_path_separators(self) -> None:
        source = (PROJECT_ROOT / "cmake/InstallRuntimeClosure.cmake.in").read_text()
        match = re.search(r'POST_EXCLUDE_REGEXES\s+("[^"]*"|\[\[.*?\]\])', source, re.S)
        self.assertIsNotNone(match)
        self.run_cmake("set(system_filter " + match[1] + ")\n" + r'''
            foreach(system_path IN ITEMS
                    [[C:\Windows\system32\msvcp140.dll]]
                    [[C:/Windows/system32/msvcp140.dll]]
                    [[C:\WINDOWS\System32\KERNEL32.dll]]
                    [[C:\Windows\system32/vcruntime140_1.dll]])
                if(NOT system_path MATCHES "${system_filter}")
                    message(FATAL_ERROR "System DLL was not excluded: ${system_path}")
                endif()
            endforeach()
            foreach(app_path IN ITEMS
                    [[D:/a/GeoReader/vcpkg_installed/x64-windows-release/bin/msvcp140.dll]]
                    [[D:\a\GeoReader\vcpkg_installed\x64-windows-release\bin\msvcp140.dll]])
                if(app_path MATCHES "${system_filter}")
                    message(FATAL_ERROR "Packaged DLL was excluded: ${app_path}")
                endif()
            endforeach()
        ''')

    def test_macos_resolves_only_used_frameworks_from_the_selected_qt_sdk(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory).resolve()
            source = root / "stage/GeoReader.app"
            destination = root / "dist/GeoReader.app"
            qt = root / "Qt/6.8.3/macos"
            runtime = root / "vcpkg_installed/arm64-osx-release"

            def fixture(path: Path, data: bytes = b"runtime fixture") -> None:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)

            fixture(source / "Contents/Info.plist", plistlib.dumps({"LSMinimumSystemVersion": "15.0"}))
            fixture(source / "Contents/MacOS/GeoReader")
            for plugin in ("platforms/libqcocoa.dylib", "tls/libqsecuretransportbackend.dylib", "iconengines/libqsvgicon.dylib"):
                fixture(qt / "plugins" / plugin)
            for plugin in ("gdal+ogr.input", "geojson.input", "shape.input"):
                fixture(runtime / "lib/mapnik/input" / plugin)
            for name in ("QtCore", "QtNetwork", "QtQuick"):
                fixture(qt / f"lib/{name}.framework/Versions/A/{name}")
            fixture(runtime / "share/georeader-driver-profile.json", b"{}")
            fixture(runtime / "share/gdal/gdalvrt.xsd")
            for name in ("proj.db", "proj.ini"):
                fixture(runtime / "share/proj" / name)

            # Supply Mach-O metadata; filesystem copying, dependency traversal,
            # framework layout and the output manifest use the real bundler.
            def metadata(*arguments: str) -> str:
                if arguments == ("qtpaths", "--plugin-dir"):
                    return str(qt / "plugins")
                if arguments == ("qtpaths", "--query", "QT_INSTALL_LIBS"):
                    return str(qt / "lib")
                if arguments[0] == "otool":
                    path = Path(arguments[2])
                    if arguments[1] == "-l":
                        return "Load command 0\ncmd LC_BUILD_VERSION\nminos 15.0\n"
                    if arguments[1] == "-D":
                        return str(path) + ":"
                    if arguments[1] == "-L":
                        name = {"GeoReader": "QtNetwork", "QtNetwork": "QtCore"}.get(path.name)
                        return str(path) + ":\n" + (
                            f"\t@rpath/{name}.framework/Versions/A/{name} (compatibility version 6.0.0)" if name else ""
                        )
                raise AssertionError(arguments)

            with mock.patch.dict(os.environ, {
                "GEOREADER_VCPKG_RUNTIME_ROOT": str(runtime),
                "GEOREADER_GDAL_PREFIX": str(runtime),
            }), mock.patch.object(bundle_macos, "run", side_effect=metadata), mock.patch.object(bundle_macos.subprocess, "run"):
                bundle_macos.bundle(source, destination)

            manifest = json.loads(destination.with_suffix(".dependencies.json").read_text())
            self.assertTrue((destination / "Contents/Frameworks/QtNetwork.framework/Versions/A/QtNetwork").is_file())
            self.assertEqual((destination / "Contents/Frameworks/QtNetwork.framework/Versions/A/QtNetwork").read_bytes(), b"runtime fixture")
            self.assertTrue((destination / "Contents/Frameworks/QtCore.framework/Versions/A/QtCore").is_file())
            self.assertFalse((destination / "Contents/Frameworks/QtQuick.framework").exists())
            self.assertEqual(len(manifest["libraries"]), 9)


if __name__ == "__main__":
    unittest.main()
