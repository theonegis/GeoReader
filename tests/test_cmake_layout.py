from __future__ import annotations

import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class CMakeLayoutTests(unittest.TestCase):

    def test_local_copyright_material_and_readme_screenshot(self) -> None:
        ignore = (PROJECT_ROOT / ".gitignore").read_text(encoding="utf-8")
        readme = (PROJECT_ROOT / "README.md").read_text(encoding="utf-8")

        self.assertIn("/docs/软件著作权/", ignore)
        self.assertIn("docs/images/main.png", readme)
        self.assertNotIn("docs/images/scientific-view.png", readme)

    def test_cross_platform_runtime_packaging_guards(self) -> None:
        closure = (PROJECT_ROOT / "cmake" / "InstallRuntimeClosure.cmake.in").read_text(
            encoding="utf-8"
        )
        macos_bundle = (PROJECT_ROOT / "scripts" / "bundle_macos.py").read_text(
            encoding="utf-8"
        )
        workflow = (PROJECT_ROOT / ".github" / "workflows" / "package.yml").read_text(
            encoding="utf-8"
        )

        self.assertIn("_mapnik_runtime_roots", closure)
        self.assertIn('"${_root}/bin/*mapnik*.dll"', closure)
        self.assertIn("LIBRARIES ${_mapnik_runtime_roots}", closure)
        self.assertIn("string(FIND", closure)
        self.assertIn("_root_position EQUAL 0", closure)
        self.assertIn("xattr', '-cr'", macos_bundle)
        self.assertIn("--timestamp=none", macos_bundle)
        self.assertIn("def checked_run", macos_bundle)
        self.assertIn("Get-ChildItem -Path $installDir -Recurse", workflow)
        self.assertIn('-ArgumentList "/S /D=$installDir"', workflow)

    def test_native_widgets_shell_uses_pinned_qlementine(self) -> None:
        cmake_source = (PROJECT_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        main_source = (PROJECT_ROOT / "src" / "main.cpp").read_text(
            encoding="utf-8"
        )
        main_window = (PROJECT_ROOT / "src" / "MainWindow.h").read_text(
            encoding="utf-8"
        )
        map_canvas = (PROJECT_ROOT / "src" / "MapCanvas.h").read_text(
            encoding="utf-8"
        )

        self.assertIn("Qt6::Widgets", cmake_source)
        self.assertIn("Qt6::Svg", cmake_source)
        self.assertIn("qlementine/tar.gz/refs/tags/v1.4.2", cmake_source)
        self.assertIn("SHA256=ed5196e6e04614db65f9a5f813eb35bf", cmake_source)
        self.assertIn("qlementine", cmake_source)
        self.assertIn('SOURCE_SUBDIR "__georeader_populate_only"', cmake_source)
        self.assertIn('"${qlementine_BINARY_DIR}" EXCLUDE_FROM_ALL', cmake_source)
        self.assertIn("class MainWindow final : public QMainWindow", main_window)
        self.assertIn("class MapCanvas final : public QWidget", map_canvas)
        self.assertIn("QlementineStyle", main_source)
        self.assertFalse((PROJECT_ROOT / "qml" / "Main.qml").exists())

        for removed in (
            "qt_add_qml_module",
            "qt_generate_deploy_qml_app_script",
            "Qt6::Qml",
            "Qt6::Quick",
            "Qt6::QuickControls2",
        ):
            self.assertNotIn(removed, cmake_source)

    def test_floating_widgets_and_explicit_pan_mode(self) -> None:
        main_window = (PROJECT_ROOT / "src" / "MainWindow.cpp").read_text(
            encoding="utf-8"
        )
        map_header = (PROJECT_ROOT / "src" / "MapCanvas.h").read_text(
            encoding="utf-8"
        )
        map_source = (PROJECT_ROOT / "src" / "MapCanvas.cpp").read_text(
            encoding="utf-8"
        )

        self.assertIn("new FrostedToolBar(m_canvas, m_mapHost)", main_window)
        self.assertIn("auto *floatingFrame = new FloatingPanelFrame(m_mapHost)", main_window)
        self.assertIn("floatingPanelClose", main_window)
        self.assertIn("dataMapSeparator", main_window)
        self.assertIn("mapSettingsSeparator", main_window)
        self.assertIn("source.width() / 9", main_window)
        self.assertIn("class LayerTreeWidget final : public QTreeWidget", main_window)
        self.assertIn("scientific-group", (PROJECT_ROOT / "tests" / "ScientificUiTests.cpp").read_text(encoding="utf-8"))
        self.assertIn("m_selectedVectorLayer = remappedIndex", main_window)
        self.assertIn("m_emptyState = new QWidget(m_mapHost)", main_window)
        self.assertIn("coordinateBadge", main_window)
        self.assertIn("configureTableTypography", main_window)
        self.assertIn("m_fontSizeSpin->setRange(10, 18)", main_window)
        for section_name in (
            "layerListSection",
            "selectedLayerSection",
            "rasterLocationSection",
            "rasterResultsSection",
            "vectorLayerSection",
            "vectorFeatureSection",
            "appearanceSection",
            "shortcutsSection",
            "aboutSection",
        ):
            self.assertIn(section_name, main_window)
        self.assertNotIn("QDockWidget", main_window)
        self.assertIn('QString m_inspectionMode = QStringLiteral("pan")', map_header)
        self.assertIn("m_panPressActive", map_header)
        self.assertIn("event->buttons() & Qt::LeftButton", map_source)
        self.assertIn("grabGesture(Qt::PinchGesture)", map_source)
        self.assertIn("QEvent::NativeGesture", map_source)
        self.assertIn("Qt::ZoomNativeGesture", map_source)


if __name__ == "__main__":
    unittest.main()
