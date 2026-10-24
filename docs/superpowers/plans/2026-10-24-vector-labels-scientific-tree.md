# Vector Labels and Scientific Layer Tree Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add independent vector color alpha, configurable vector labels, multi-variable scientific import with a grouped layer tree, stable user-resizable panel width, and refreshed repository documentation.

**Architecture:** Extend `LayerSnapshot` as the single rendering/style contract, render labels in the offscreen vector pass, batch scientific import selections through `AppController`, and replace the flat presentation-only list with a dataset-aware tree backed by the unchanged flat `LayerModel`. Keep panel width as explicit persisted UI state rather than deriving it from selected content.

**Tech Stack:** C++20, Qt 6 Widgets/QtConcurrent, GDAL/OGR, Mapnik, CMake/CTest, Python unittest.

**Spec:** `docs/superpowers/specs/2026-10-24-vector-labels-scientific-tree-design.md`

## Global Constraints

- Preserve current user changes under `docs/images/` and never add `docs/软件著作权/` to Git.
- Preserve stable layer IDs, rendering order, coordinate-mode confirmation, and existing scientific slicing/time-series behavior.
- Support macOS 15+, Windows 10 x64 1809+, Fedora 43+, and Ubuntu 24.04 LTS+ without a new runtime dependency.
- Use tests before production changes and keep UI interactions consistent with existing native dialogs and floating panels.

## Review Focus

- A fully transparent fill with an opaque outline must keep the outline visible.
- Empty/null label fields and invalid geometries must be skipped without breaking the vector render.
- Multiple selected arrays with different dimension orders must retain their own X/Y and slice settings.
- Parent visibility must become partially checked when only some dataset children are visible.
- Window narrowing must clamp a remembered panel width without overwriting the user's preferred width.

---

### Task 1: Vector style alpha and label contract

**Files:**
- Modify: `src/LayerModel.h`
- Modify: `src/LayerModel.cpp`
- Modify: `tests/ModelTests.cpp`

**Interfaces:**
- Produces: `LayerSnapshot::{labelEnabled,labelField,labelFontFamily,labelFontSize,labelPlacement}` and `LayerModel::setVectorLabel(...)`.

- [ ] Add failing model assertions for independent color alpha and normalized label properties.
- [ ] Run `cmake --build build --target GeoReaderModelTests && ./build/GeoReaderModelTests` and verify the new assertions fail.
- [ ] Add roles, serialization, normalization, and update signals for vector labels.
- [ ] Re-run the model target and verify it passes.

### Task 2: Vector style UI and label rendering

**Files:**
- Create: `src/VectorLabelRenderer.h`
- Create: `src/VectorLabelRenderer.cpp`
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`
- Modify: `src/MapCanvas.cpp`
- Modify: `CMakeLists.txt`
- Modify: `tests/MapCanvasInteractionTests.cpp`
- Modify: `tests/ScientificUiTests.cpp`

**Interfaces:**
- Consumes: Task 1 label properties.
- Produces: alpha-aware native color controls and `VectorLabelRenderer::draw(QImage &, const LayerSnapshot &, const MapViewport &)`.

- [ ] Add failing renderer/UI assertions for color alpha, label controls, and geometry-dependent placement choices.
- [ ] Run the focused interaction/UI tests and confirm failure is due to missing behavior.
- [ ] Enable `QColorDialog::ShowAlphaChannel`, display ARGB/checkerboard swatches, populate vector fields, and wire label controls to `setVectorLabel`.
- [ ] Implement projected feature anchors, halo text drawing, and same-layer collision suppression in `VectorLabelRenderer`; compose it after each Mapnik vector layer.
- [ ] Run the focused tests and verify they pass.

### Task 3: Batch scientific import

**Files:**
- Modify: `src/AppController.h`
- Modify: `src/AppController.cpp`
- Modify: `src/MainWindow.cpp`
- Modify: `tests/ModelTests.cpp`
- Modify: `tests/ScientificUiTests.cpp`

**Interfaces:**
- Produces: `AppController::confirmMultidimensionalImports(const QVariantList &)` accepting per-variable maps with array, axes, slices, coordinate mode, and CRS.

- [ ] Add failing tests that select two arrays and expect two visible layers sharing one dataset ID.
- [ ] Run model/UI targets and confirm the multi-select expectations fail.
- [ ] Replace the single combo dialog with a checkable variable tree and per-variable saved settings.
- [ ] Batch-prepare selected variables off the UI thread, add successes under one dataset ID, and report partial failures.
- [ ] Re-run focused tests and verify existing single-variable imports still pass.

### Task 4: Dataset-aware layer tree and stable resizable width

**Files:**
- Modify: `src/MainWindow.h`
- Modify: `src/MainWindow.cpp`
- Modify: `tests/ScientificUiTests.cpp`

**Interfaces:**
- Consumes: existing `LayerModel` dataset APIs and Task 3 shared dataset IDs.
- Produces: `layerTree` with dataset parents/children and persisted `ui/layerPanelWidth`.

- [ ] Add failing UI assertions for grouped children, partial parent checks, no width change on raster/vector selection, and left-edge resize.
- [ ] Run `GeoReaderScientificUI` and confirm those assertions fail for the current flat list/content-derived width.
- [ ] Implement stable-ID tree refresh, parent/child visibility, hover deletion, and dataset/sibling reordering.
- [ ] Add the left-edge resize gesture and use the persisted preferred layer width in `positionFloatingUi()` without changing it during clamping.
- [ ] Re-run the UI test and verify drag/delete/selection regressions remain green.

### Task 5: Local-only copyright files and README refresh

**Files:**
- Modify: `.gitignore`
- Modify: `README.md`
- Modify: `tests/test_cmake_layout.py`

**Interfaces:**
- Produces: ignored `docs/软件著作权/` and README using `docs/images/main.png` plus updated feature text.

- [ ] Add failing repository assertions for the ignore rule and refreshed screenshot path.
- [ ] Run `python3 -m unittest tests.test_cmake_layout` and confirm failure.
- [ ] Add the anchored ignore rule and update README features/screenshot without adding local copyright files.
- [ ] Re-run Python tests and verify the copyright folder is absent from `git status --untracked-files=all`.

### Task 6: Full verification

**Files:**
- Verify all modified files.

**Interfaces:**
- Consumes: Tasks 1-5.
- Produces: reviewable, uncommitted implementation with test evidence.

- [ ] Run `python3 -m unittest discover -s tests -p 'test_*.py'`.
- [ ] Run `cmake --build build --parallel 6`.
- [ ] Run `ctest --test-dir build --output-on-failure --timeout 90`.
- [ ] Run `git diff --check`, inspect `git status --short`, and confirm `docs/软件著作权/` is not listed.
- [ ] Review the final diff against all five user requirements and record any platform-specific limitation.
