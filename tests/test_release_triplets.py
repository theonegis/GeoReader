from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class ReleaseTripletTests(unittest.TestCase):
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


if __name__ == "__main__":
    unittest.main()
