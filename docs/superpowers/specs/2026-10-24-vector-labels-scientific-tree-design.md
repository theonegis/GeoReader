# Vector Labels and Scientific Layer Tree Design

## Intent

GeoReader should treat vector presentation and multidimensional files as first-class professional GIS data. Vector outlines and fills need independent transparency, vector layers need configurable labels, and one scientific file should be able to contribute several independently visible variable layers. The layer panel must remain visually stable and user-resizable.

## Goals

- Preserve alpha in line, fill, and point colors independently from whole-layer opacity.
- Add layer-level labels with an attribute field, font family, point size, and geometry-aware placement. Labels are disabled by default and use the geometry center by default when enabled.
- Let users select several arrays in one multidimensional import. Import only checked arrays, give them one shared dataset identity, and show multi-layer datasets as a file node with checkable layer children.
- Keep the layer panel at a stable width. Its left edge is the only resize handle, and a user-selected width is remembered in application settings.
- Keep `docs/软件著作权/` local, update the README screenshot to the refreshed image set, and leave the README focused on features and compilation.

## Non-goals

- Loading every unselected scientific variable lazily after the import dialog closes.
- A full QGIS PAL collision/priority engine, data-defined label expressions, curved glyph placement, or per-feature label styling.
- Replacing the current map renderer or changing supported file formats.

## Constraints

- Qt 6 Widgets remains the UI framework; Mapnik/GDAL remain the spatial backends.
- The release continues to target macOS 15+, Windows 10 x64 1809+, Fedora 43+, and Ubuntu 24.04 LTS+.
- Existing layer order, stable IDs, coordinate-mode isolation, and scientific pixel/time-series behavior must remain intact.
- Existing user screenshot changes are preserved. The copyright folder is ignored without deleting local files.

## Approaches

1. **Selected arrays become real child layers (chosen).** The import dialog uses a checkable variable list. Each checked variable has its own axis/slice settings, all prepared views share one dataset ID, and the layer tree groups them under the source file. This keeps resource use proportional to the user's choice and uses the current rendering model.
2. Import every variable eagerly and hide unchecked variables. This makes later toggling instant but can open many expensive arrays and generate unnecessary cached views.
3. Add catalog-only nodes and build views when first checked. This best resembles dedicated HDF viewers, but requires a new lazy-loading state machine and error states in the layer model.

## Interaction and Rendering

- Color buttons open Qt's native color dialog with the alpha channel enabled. Button text includes an eight-digit ARGB value and the swatch uses a checkerboard so transparency is visible.
- Label controls appear within the existing vector style section: enable, field, font, size, and position. Point layers offer center/above/below/left/right; line layers offer center/start/end/above/below; polygon layers offer centroid/interior/above/below/boundary. The renderer obtains one anchor per feature, projects it to the current viewport, draws a subtle halo, and avoids overlapping label rectangles within that vector layer.
- The multidimensional dialog uses a checkable variable tree. Selecting a row edits that variable's X/Y axes, fixed indices, coordinate mode, and CRS. Import is disabled until at least one valid variable is checked. Preparation runs off the GUI thread and successful variables are added under one dataset node; failures are reported without discarding successful siblings.
- The layer panel uses a tree. A dataset with multiple layers is a top-level file node; its children are actual layers. Single-layer datasets remain one top-level layer. Parent and child check states control dataset and layer visibility respectively. Stable IDs remain the source of truth for selection, deletion, and reordering.
- The floating panel has one remembered width per panel category. The layer panel no longer derives width from the selected layer type. Dragging the left six pixels updates and persists the layer-panel width within window-size limits.

## Validation

- Model tests cover alpha retention and all label properties.
- Renderer tests or deterministic render helpers cover independent fill/line alpha and label placement anchors.
- Controller/UI tests cover multi-variable import, shared dataset identity, grouped tree visibility, and selection stability.
- UI tests reproduce the layer-panel width jump, then verify stable width and left-edge resizing.
- Python repository tests verify the ignore rule and README image target.
