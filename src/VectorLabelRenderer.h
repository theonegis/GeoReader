#pragma once

#include <QColor>

class QImage;
struct LayerSnapshot;
struct MapViewport;

namespace VectorLabelRenderer {
[[nodiscard]] double combinedOpacity(const QColor &color, double layerOpacity);
[[nodiscard]] int draw(QImage &image, const LayerSnapshot &layer,
                       const MapViewport &viewport);
}
