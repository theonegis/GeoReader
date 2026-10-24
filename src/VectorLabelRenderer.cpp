#include "VectorLabelRenderer.h"

#include "LayerModel.h"
#include "MapCanvas.h"

#include <QFont>
#include <QFontMetrics>
#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <memory>

namespace {
using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
struct TransformDeleter {
    void operator()(OGRCoordinateTransformation *transform) const
    {
        if (transform)
            OGRCoordinateTransformation::DestroyCT(transform);
    }
};
using TransformPtr = std::unique_ptr<OGRCoordinateTransformation,
                                     TransformDeleter>;

bool featureAnchor(const OGRGeometry *geometry, const QString &placement,
                   OGRPoint &anchor)
{
    if (!geometry || geometry->IsEmpty())
        return false;
    const OGRwkbGeometryType type = wkbFlatten(geometry->getGeometryType());
    if (type == wkbPoint) {
        anchor = *geometry->toPoint();
        return true;
    }
    if (type == wkbLineString) {
        const auto *line = geometry->toLineString();
        if (!line || line->getNumPoints() == 0)
            return false;
        if (placement == QStringLiteral("start"))
            line->getPoint(0, &anchor);
        else if (placement == QStringLiteral("end"))
            line->getPoint(line->getNumPoints() - 1, &anchor);
        else
            line->Value(line->get_Length() * 0.5, &anchor);
        return true;
    }
    if (type == wkbPolygon && placement == QStringLiteral("boundary")) {
        const auto *ring = geometry->toPolygon()->getExteriorRing();
        if (ring && ring->getNumPoints() > 0) {
            ring->Value(ring->get_Length() * 0.5, &anchor);
            return true;
        }
    }
    if (type == wkbPolygon && placement == QStringLiteral("interior")
        && geometry->toPolygon()->PointOnSurface(&anchor) == OGRERR_NONE)
        return true;
    return geometry->Centroid(&anchor) == OGRERR_NONE;
}

QPointF screenPoint(const OGRPoint &point, const MapViewport &viewport)
{
    return {(point.getX() - viewport.minMercatorX)
                / (viewport.maxMercatorX - viewport.minMercatorX) * viewport.width,
            (viewport.maxMercatorY - point.getY())
                / (viewport.maxMercatorY - viewport.minMercatorY) * viewport.height};
}

QRectF positionedRect(const QPointF &anchor, const QSize &size,
                      const QString &placement)
{
    constexpr double gap = 7.0;
    QPointF topLeft(anchor.x() - size.width() / 2.0,
                    anchor.y() - size.height() / 2.0);
    if (placement == QStringLiteral("above"))
        topLeft.setY(anchor.y() - size.height() - gap);
    else if (placement == QStringLiteral("below"))
        topLeft.setY(anchor.y() + gap);
    else if (placement == QStringLiteral("left"))
        topLeft.setX(anchor.x() - size.width() - gap);
    else if (placement == QStringLiteral("right"))
        topLeft.setX(anchor.x() + gap);
    return {topLeft, QSizeF(size)};
}
}

double VectorLabelRenderer::combinedOpacity(const QColor &color,
                                            double layerOpacity)
{
    return std::clamp(color.alphaF() * layerOpacity, 0.0, 1.0);
}

int VectorLabelRenderer::draw(QImage &image, const LayerSnapshot &layer,
                              const MapViewport &viewport)
{
    if (!layer.labelEnabled || layer.labelField.trimmed().isEmpty()
        || image.isNull() || !viewport.isValid())
        return 0;
    DatasetPtr dataset(static_cast<GDALDataset *>(GDALOpenEx(
                           layer.path.toUtf8().constData(),
                           GDAL_OF_VECTOR | GDAL_OF_READONLY,
                           nullptr, nullptr, nullptr)), GDALClose);
    if (!dataset)
        return 0;
    OGRLayer *source = layer.sourceLayer.isEmpty()
        ? dataset->GetLayer(0)
        : dataset->GetLayerByName(layer.sourceLayer.toUtf8().constData());
    if (!source || !source->GetLayerDefn()
        || source->GetLayerDefn()->GetFieldIndex(
               layer.labelField.toUtf8().constData()) < 0)
        return 0;

    OGRSpatialReference target;
    target.importFromEPSG(3857);
    target.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    const OGRSpatialReference *sourceReference = source->GetSpatialRef();
    if (!sourceReference)
        return 0;
    std::unique_ptr<OGRSpatialReference> sourceReferenceCopy(sourceReference->Clone());
    sourceReferenceCopy->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    TransformPtr transform(
        OGRCreateCoordinateTransformation(sourceReferenceCopy.get(), &target));
    if (!transform)
        return 0;

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    QFont font(layer.labelFontFamily);
    font.setPointSize(std::clamp(layer.labelFontSize, 6, 72));
    painter.setFont(font);
    const QFontMetrics metrics(font);
    QVector<QRectF> occupied;
    int rendered = 0;
    source->ResetReading();
    while (auto *rawFeature = source->GetNextFeature()) {
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>
            feature(rawFeature, OGRFeature::DestroyFeature);
        const int field = feature->GetFieldIndex(layer.labelField.toUtf8().constData());
        if (field < 0 || !feature->IsFieldSetAndNotNull(field))
            continue;
        const QString text = QString::fromUtf8(feature->GetFieldAsString(field)).trimmed();
        if (text.isEmpty() || !feature->GetGeometryRef())
            continue;
        std::unique_ptr<OGRGeometry> geometry(feature->GetGeometryRef()->clone());
        if (!geometry || geometry->transform(transform.get()) != OGRERR_NONE)
            continue;
        OGRPoint anchor;
        if (!featureAnchor(geometry.get(), layer.labelPlacement, anchor))
            continue;
        const QSize textSize = metrics.size(Qt::TextSingleLine, text)
                                   .grownBy(QMargins(4, 2, 4, 2));
        QRectF rect = positionedRect(screenPoint(anchor, viewport), textSize,
                                     layer.labelPlacement);
        if (!QRectF(image.rect()).intersects(rect)
            || std::any_of(occupied.cbegin(), occupied.cend(),
                           [&rect](const QRectF &other) {
                               return other.adjusted(-2, -2, 2, 2).intersects(rect);
                           }))
            continue;
        occupied.push_back(rect);
        QPainterPath path;
        path.addText(rect.left() + 2, rect.top() + 2 + metrics.ascent(), font, text);
        QColor halo(Qt::white);
        halo.setAlphaF(std::clamp(layer.opacity * 0.9, 0.0, 1.0));
        painter.setPen(QPen(halo, 3.0, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(path);
        QColor foreground(Qt::black);
        foreground.setAlphaF(std::clamp(layer.opacity, 0.0, 1.0));
        painter.fillPath(path, foreground);
        ++rendered;
    }
    return rendered;
}
