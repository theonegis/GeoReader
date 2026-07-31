from __future__ import annotations

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class ReleaseTripletTests(unittest.TestCase):
    def test_mapnik_config_links_all_exported_implementation_targets(
        self,
    ) -> None:
        cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn(
            "foreach(_mapnik_target IN ITEMS mapnik::json mapnik::wkt)",
            cmake,
        )
        self.assertIn(
            "list(APPEND GEOREADER_MAPNIK_LINK_TARGETS "
            '"${_mapnik_target}")',
            cmake,
        )
        self.assertIn("${GEOREADER_MAPNIK_LINK_TARGETS}", cmake)

    def test_linux_qt_deploy_selects_portable_plugins(self) -> None:
        cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        for plugin in (
            "qoffscreen",
            "qwayland",
            "qwayland-egl",
            "qwayland-generic",
        ):
            self.assertIn(plugin, cmake)
        self.assertIn(
            "Qt6_VERSION VERSION_GREATER_EQUAL 6.10",
            cmake,
        )
        self.assertIn("Qt6::QOffscreenIntegrationPlugin", cmake)
        self.assertIn("Qt6::QWaylandIntegrationPlugin", cmake)
        self.assertIn("install(IMPORTED_RUNTIME_ARTIFACTS", cmake)
        self.assertIn(
            "EXCLUDE_PLUGINS libfcitx5platforminputcontextplugin",
            cmake,
        )
        self.assertIn(
            "EXCLUDE_PLUGIN_TYPES platformthemes styles",
            cmake,
        )

    def test_linux_cpack_stages_qt_deployment_with_destdir(self) -> None:
        cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("set(CPACK_SET_DESTDIR ON)", cmake)
        self.assertIn("set(CPACK_PACKAGE_RELOCATABLE OFF)", cmake)
        self.assertIn("Qt6_VERSION VERSION_LESS 6.9", cmake)
        self.assertIn("cmake/RunQt68Deploy.cmake.in", cmake)

    def test_qt68_deploy_wrapper_uses_cpack_staging_prefix(self) -> None:
        template = (
            PROJECT_ROOT / "cmake" / "RunQt68Deploy.cmake.in"
        ).read_text(encoding="utf-8")

        with tempfile.TemporaryDirectory() as temporary_directory:
            temporary_path = Path(temporary_directory)
            staging_root = temporary_path / "cpack-stage"
            deploy_script = temporary_path / "fake-qt68-deploy.cmake"
            wrapper_script = temporary_path / "run-qt68-deploy.cmake"
            plugin_source = temporary_path / "libqoffscreen.so"
            expected_prefix = staging_root / "usr"
            plugin_source.write_text("fixture", encoding="utf-8")

            deploy_script.write_text(
                "\n".join(
                    (
                        'if(NOT "$ENV{DESTDIR}" STREQUAL "")',
                        '  message(FATAL_ERROR "DESTDIR was not cleared")',
                        "endif()",
                        'if(NOT CMAKE_INSTALL_PREFIX STREQUAL '
                        f'"{expected_prefix}")',
                        '  message(FATAL_ERROR "Incorrect install prefix: '
                        '${CMAKE_INSTALL_PREFIX}")',
                        "endif()",
                        'if(NOT QT_DEPLOY_PREFIX STREQUAL '
                        f'"{expected_prefix}")',
                        '  message(FATAL_ERROR "Incorrect Qt deploy prefix: '
                        '${QT_DEPLOY_PREFIX}")',
                        "endif()",
                        f'file(INSTALL "{plugin_source}" DESTINATION '
                        '"${CMAKE_INSTALL_PREFIX}/plugins/platforms")',
                    )
                ),
                encoding="utf-8",
            )
            wrapper_script.write_text(
                template.replace(
                    "@GEOREADER_QT_DEPLOY_SCRIPT@",
                    deploy_script.as_posix(),
                ),
                encoding="utf-8",
            )

            environment = os.environ.copy()
            environment["DESTDIR"] = str(staging_root)
            subprocess.run(
                [
                    "cmake",
                    "-DCMAKE_INSTALL_PREFIX=/usr",
                    "-P",
                    str(wrapper_script),
                ],
                check=True,
                env=environment,
                capture_output=True,
                text=True,
                timeout=30,
            )

            self.assertTrue(
                (
                    expected_prefix
                    / "plugins"
                    / "platforms"
                    / "libqoffscreen.so"
                ).is_file()
            )

    def test_ci_triplets_only_build_release_dependencies(self) -> None:
        for platform in ("linux", "windows"):
            triplet = (
                PROJECT_ROOT
                / "cmake"
                / "triplets"
                / f"x64-{platform}-release.cmake"
            ).read_text(encoding="utf-8")
            self.assertIn("set(VCPKG_BUILD_TYPE release)", triplet)

    def test_linux_triplet_explicitly_targets_linux(self) -> None:
        triplet = (
            PROJECT_ROOT
            / "cmake"
            / "triplets"
            / "x64-linux-release.cmake"
        ).read_text(encoding="utf-8")
        self.assertIn("set(VCPKG_CMAKE_SYSTEM_NAME Linux)", triplet)

    def test_workflow_uses_project_release_triplets(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        for platform in ("linux", "windows"):
            self.assertIn(f"x64-{platform}-release", workflow)
        self.assertIn("VCPKG_OVERLAY_TRIPLETS", workflow)
        self.assertIn("actions/cache/restore@v4", workflow)
        self.assertIn("actions/cache/save@v4", workflow)

    def test_windows_workflow_forces_msvc_and_vcpkg_pkgconf(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("ilammy/msvc-dev-cmd@", workflow)
        self.assertIn("-DCMAKE_CXX_COMPILER=cl.exe", workflow)
        self.assertIn("-DPKG_CONFIG_EXECUTABLE=$($pkgConfig.FullName)", workflow)
        self.assertIn("PKG_CONFIG_PATH", workflow)

    def test_linux_workflow_validates_packaged_runtime(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        for expected in (
            "dpkg-deb -x",
            'rpm -qpl "$rpm_package"',
            'ldd "$runtime_file"',
            "--smoke-test",
            "mapnik/input/geojson",
            "mapnik/input/raster",
            "mapnik/input/shape",
            "libqoffscreen",
            "libqwayland(",
            "libcomposeplatforminputcontextplugin",
            "libibusplatforminputcontextplugin",
        ):
            self.assertIn(expected, workflow)


if __name__ == "__main__":
    unittest.main()
