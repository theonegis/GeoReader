from __future__ import annotations

import re
import unittest
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parents[1]


class CMakeLayoutTests(unittest.TestCase):
    def test_qml_output_does_not_shadow_linux_executable(self) -> None:
        cmake_source = (PROJECT_ROOT / "CMakeLists.txt").read_text(
            encoding="utf-8"
        )
        output_match = re.search(
            r'set\(QT_QML_OUTPUT_DIRECTORY\s+'
            r'"\$\{CMAKE_CURRENT_BINARY_DIR\}/([^"]+)"\)',
            cmake_source,
        )

        self.assertIsNotNone(
            output_match,
            "QML 工具产物必须放在构建根目录之外的独立子目录中。",
        )
        self.assertEqual(output_match.group(1), "qml")
        self.assertLess(
            output_match.start(),
            cmake_source.index("qt_add_qml_module(GeoReader"),
            "QT_QML_OUTPUT_DIRECTORY 必须在创建 QML 模块前设置。",
        )


if __name__ == "__main__":
    unittest.main()
