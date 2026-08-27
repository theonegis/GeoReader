#pragma once

#include "LayerModel.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QPointF>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QTimer>
#include <QWidget>

class QNetworkReply;

struct MapViewport
{
    QString coordinateMode = QStringLiteral("geographic");
    int width = 0;
    int height = 0;
    double minMercatorX = 0.0;
    double minMercatorY = 0.0;
    double maxMercatorX = 0.0;
    double maxMercatorY = 0.0;

    [[nodiscard]] bool isValid() const noexcept
    {
        return width > 0 && height > 0
               && minMercatorX < maxMercatorX
               && minMercatorY < maxMercatorY;
    }
};

struct RenderResult
{
    quint64 generation = 0;
    QImage image;
    QString error;
    MapViewport viewport;
};

class MapCanvas final : public QWidget
{
    Q_OBJECT

public:
    explicit MapCanvas(QWidget *parent = nullptr);
    ~MapCanvas() override;

    LayerModel *layerModel() const { return m_layerModel; }
    void setLayerModel(LayerModel *model);
    double centerLongitude() const { return m_centerLongitude; }
    double centerLatitude() const { return m_centerLatitude; }
    double zoomLevel() const { return m_zoomLevel; }
    double mouseLongitude() const { return m_mouseLongitude; }
    double mouseLatitude() const { return m_mouseLatitude; }
    bool rendering() const { return m_rendering; }
    bool rectangleZoomActive() const { return m_rectangleZoomActive; }
    QString inspectionMode() const { return m_inspectionMode; }
    QString baseMap() const { return m_baseMap; }
    QString baseMapAttribution() const;
    QString coordinateMode() const { return m_coordinateMode; }
    bool wheelZoomEnabled() const { return m_wheelZoomEnabled; }
    int attributionRightInset() const { return m_attributionRightInset; }

    void zoomBy(double delta);
    void panBy(double horizontalPixels, double verticalPixels);
    void fitBounds(double minLon, double minLat,
                   double maxLon, double maxLat);
    void setRectangleZoomActive(bool active);
    void setInspectionMode(const QString &mode);
    void setBaseMap(const QString &baseMap);
    void setCoordinateMode(const QString &mode,
                           int pixelWidth = 0, int pixelHeight = 0);
    void setAttributionRightInset(int inset);
    void setSelectedFeatureWkt(const QString &wkt);
    void clearSelectedFeature();
    void refresh();
    void setWheelZoomEnabled(bool enabled);

signals:
    void layerModelChanged();
    void viewportChanged();
    void mouseCoordinateChanged();
    void renderingChanged();
    void rectangleZoomActiveChanged();
    void inspectionModeChanged();
    void baseMapChanged();
    void coordinateModeChanged();
    void wheelZoomEnabledChanged();
    void mapClicked(double longitude, double latitude);
    void renderError(const QString &message);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;

private:
    static QPointF lonLatToWorld(double longitude, double latitude, double zoom);
    static QPointF worldToLonLat(double x, double y, double zoom);
    static QPointF lonLatToMercator(double longitude, double latitude);
    static RenderResult renderLayers(QVector<LayerSnapshot> layers,
                                     MapViewport viewport, quint64 generation);

    QPointF screenToLonLat(const QPointF &screenPoint) const;
    MapViewport currentViewport() const;
    void updateMouseCoordinate(const QPointF &position);
    void updateCursor();
    void scheduleOverlayRender();
    void beginOverlayRender();
    void drawBaseMap(QPainter *painter);
    void drawSelectedFeature(QPainter *painter) const;
    void requestTile(int zoom, int x, int y, const QString &key);
    void tileFinished(QNetworkReply *reply);
    void setRendering(bool rendering);

    LayerModel *m_layerModel = nullptr;
    QNetworkAccessManager m_network;
    QHash<QString, QImage> m_tiles;
    QSet<QString> m_pendingTiles;
    QTimer m_renderTimer;
    QFutureWatcher<RenderResult> m_renderWatcher;
    QImage m_overlay;
    MapViewport m_overlayViewport;
    mutable QMutex m_imageMutex;
    quint64 m_generation = 0;
    double m_centerLongitude = 0.0;
    double m_centerLatitude = 20.0;
    double m_zoomLevel = 2.0;
    double m_mouseLongitude = 0.0;
    double m_mouseLatitude = 0.0;
    QPointF m_lastMousePosition;
    QPointF m_pressPosition;
    QRectF m_selectionRectangle;
    QString m_selectedFeatureWkt;
    bool m_dragging = false;
    bool m_panPressActive = false;
    bool m_selectingRectangle = false;
    bool m_rectangleZoomActive = false;
    bool m_rendering = false;
    int m_attributionRightInset = 0;
    QString m_inspectionMode = QStringLiteral("pan");
    QString m_baseMap = QStringLiteral("osm");
    QString m_coordinateMode = QStringLiteral("geographic");
    double m_pixelCenterX = 0.0;
    double m_pixelCenterY = 0.0;
    double m_pixelUnitsPerScreenPixel = 1.0;
    bool m_wheelZoomEnabled = true;
};
