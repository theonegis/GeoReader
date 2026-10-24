#include "LayerModel.h"
#include "MapCanvas.h"
#include "VectorLabelRenderer.h"

#include <QApplication>
#include <QImage>

#include <gdal_priv.h>

#include <cmath>
#include <iostream>

namespace {
bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

QPointF mercator(double longitude, double latitude)
{
    constexpr double radius = 6378137.0;
    constexpr double degrees = 3.14159265358979323846 / 180.0;
    return {radius * longitude * degrees,
            radius * std::log(std::tan(3.14159265358979323846 / 4.0
                                       + latitude * degrees / 2.0))};
}
}

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    GDALAllRegister();

    const QColor translucent(30, 60, 90, 64);
    if (!expect(std::abs(VectorLabelRenderer::combinedOpacity(translucent, 0.5)
                         - (64.0 / 255.0 * 0.5)) < 1e-6,
                "Color alpha was not combined with whole-layer opacity")) {
        return 1;
    }

    LayerSnapshot layer;
    layer.path = QStringLiteral(GEOREADER_TEST_DATA_DIR "/demo.geojson");
    layer.sourceLayer = QStringLiteral("georeader_demo");
    layer.type = QStringLiteral("vector");
    layer.geometryType = QStringLiteral("polygon");
    layer.labelEnabled = true;
    layer.labelField = QStringLiteral("name");
    layer.labelFontFamily = QApplication::font().family();
    layer.labelFontSize = 14;
    layer.labelPlacement = QStringLiteral("center");

    const QPointF lower = mercator(120.09, 30.21);
    const QPointF upper = mercator(120.18, 30.29);
    MapViewport viewport;
    viewport.width = 640;
    viewport.height = 480;
    viewport.minMercatorX = lower.x();
    viewport.minMercatorY = lower.y();
    viewport.maxMercatorX = upper.x();
    viewport.maxMercatorY = upper.y();

    QImage labels(viewport.width, viewport.height,
                  QImage::Format_ARGB32_Premultiplied);
    labels.fill(Qt::transparent);
    const int rendered = VectorLabelRenderer::draw(labels, layer, viewport);
    int paintedPixels = 0;
    for (int y = 0; y < labels.height(); ++y)
        for (int x = 0; x < labels.width(); ++x)
            paintedPixels += qAlpha(labels.pixel(x, y)) > 0 ? 1 : 0;
    if (!expect(rendered == 1, "Expected one feature label")
        || !expect(paintedPixels > 50, "Label renderer did not paint text")) {
        return 2;
    }

    layer.labelField = QStringLiteral("missing_field");
    labels.fill(Qt::transparent);
    if (!expect(VectorLabelRenderer::draw(labels, layer, viewport) == 0,
                "Missing label fields were not skipped")) {
        return 3;
    }
    return 0;
}
