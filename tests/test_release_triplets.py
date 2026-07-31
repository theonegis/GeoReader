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

    def test_required_mapnik_plugins_are_resolved_and_installed_explicitly(
        self,
    ) -> None:
        cmake = (PROJECT_ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        for plugin in (
            "geojson.input",
            "gdal+ogr.input",
            "gdal.input",
            "ogr.input",
            "raster.input",
            "shape.input",
        ):
            self.assertIn(plugin, cmake)
        self.assertIn("GEOREADER_MAPNIK_INPUT_PLUGIN_FILES", cmake)
        self.assertIn(
            "No single Mapnik input directory contains all required plugins",
            cmake,
        )
        self.assertIn(
            "install(FILES ${GEOREADER_MAPNIK_INPUT_PLUGIN_FILES}",
            cmake,
        )
        self.assertIn("GEOREADER_MAPNIK_RUNTIME_INPUT_DIR", cmake)
        self.assertNotIn(
            'install(DIRECTORY "${MAPNIK_INPUT_PLUGIN_DIR}/"',
            cmake,
        )

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
        self.assertIn(
            "Qt6::QComposePlatformInputContextPlugin",
            cmake,
        )
        self.assertIn("Qt6::QIbusPlatformInputContextPlugin", cmake)
        self.assertIn(
            '"${CMAKE_INSTALL_LIBDIR}/qt6/plugins/platforminputcontexts"',
            cmake,
        )
        self.assertIn("IMPORTED_RUNTIME_ARTIFACTS Qt6::DBus", cmake)
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

    def test_packaging_workflow_explicitly_builds_release_only(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        self.assertEqual(workflow.count("./scripts/build.sh --type Release"), 2)
        self.assertIn(
            r".\scripts\build.ps1 -Type Release -CleanFirst",
            workflow,
        )
        self.assertNotIn("./scripts/build.sh --type Debug", workflow)
        self.assertNotIn(r".\scripts\build.ps1 -Type Debug", workflow)

    def test_linux_triplet_explicitly_targets_linux(self) -> None:
        triplet = (
            PROJECT_ROOT
            / "cmake"
            / "triplets"
            / "x64-linux-release.cmake"
        ).read_text(encoding="utf-8")
        self.assertIn("set(VCPKG_CMAKE_SYSTEM_NAME Linux)", triplet)
        self.assertIn("set(VCPKG_LIBRARY_LINKAGE dynamic)", triplet)
        self.assertNotIn("set(VCPKG_LIBRARY_LINKAGE static)", triplet)

    def test_workflow_uses_project_release_triplets(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        for platform in ("linux", "windows"):
            self.assertIn(f"x64-{platform}-release", workflow)
        self.assertIn("VCPKG_OVERLAY_TRIPLETS", workflow)
        self.assertIn("actions/cache/restore@v4", workflow)
        self.assertIn("actions/cache/save@v4", workflow)

    def test_release_commands_explicitly_target_the_repository(self) -> None:
        workflow = (
            PROJECT_ROOT / ".github" / "workflows" / "package.yml"
        ).read_text(encoding="utf-8")
        self.assertIn("release-only", workflow)
        self.assertIn("Validate release-only inputs", workflow)
        self.assertEqual(
            workflow.count('--repo "$GITHUB_REPOSITORY"'),
            3,
        )
        self.assertIn("inputs.build_target != 'release-only'", workflow)

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
            'rpm2cpio "$rpm_package"',
            "cpio --extract --make-directories --quiet",
            'ldd "$runtime_file"',
            "--smoke-test",
            "mapnik/input/geojson.input",
            "mapnik/input/raster.input",
            "mapnik/input/shape.input",
            "libqoffscreen",
            "libqwayland*.so",
            "libcomposeplatforminputcontextplugin",
            "libibusplatforminputcontextplugin",
            "libQt6DBus",
            "require_package_entry",
            "Incomplete ${package_name} package",
            "Verify vcpkg Mapnik input plugins",
            "Incomplete vcpkg Mapnik",
            "Prepare relocatable vcpkg runtime",
            "patchelf --set-rpath '$ORIGIN'",
            "patchelf --set-rpath '$ORIGIN/../..'",
            "${source_mapnik_input}.build-only",
            "if-no-files-found: warn",
            "mapnik-input-v2",
            "if: always()",
        ):
            self.assertIn(expected, workflow)


if __name__ == "__main__":
    unittest.main()
