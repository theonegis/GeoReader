#pragma once

#include "LayerModel.h"

#include <QImage>
#include <QString>

struct RasterRenderViewport
{
    int width = 0;
    int height = 0;
    double minMercatorX = 0.0;
    double minMercatorY = 0.0;
    double maxMercatorX = 0.0;
    double maxMercatorY = 0.0;
};

struct RasterRenderResult
{
    QImage image;
    QString error;
};

// Reads only the current map viewport. GDAL selects the closest available
// overview automatically; the style-independent float buffer is cached so
// color-ramp and stretch changes do not touch the source raster again.
[[nodiscard]] RasterRenderResult
renderRasterLayer(const LayerSnapshot &layer,
                  const RasterRenderViewport &viewport);
