#pragma once
#include "LayerModel.h"
#include <QImage>
struct MapViewport;
namespace RasterRenderer {
QImage render(const LayerSnapshot &layer, const MapViewport &viewport,
              QString &error);
}
