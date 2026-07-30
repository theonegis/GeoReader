#pragma once

#include "LayerModel.h"

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QMutex>
#include <QNetworkAccessManager>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QRectF>
#include <QSet>
#include <QString>
#include <QTimer>

class QNetworkReply;

struct MapViewport
{
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

class MapCanvas : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(LayerModel *layerModel READ layerModel WRITE setLayerModel NOTIFY layerModelChanged)
    Q_PROPERTY(double centerLongitude READ centerLongitude NOTIFY viewportChanged)
    Q_PROPERTY(double centerLatitude READ centerLatitude NOTIFY viewportChanged)
    Q_PROPERTY(double zoomLevel READ zoomLevel NOTIFY viewportChanged)
    Q_PROPERTY(double mouseLongitude READ mouseLongitude NOTIFY mouseCoordinateChanged)
    Q_PROPERTY(double mouseLatitude READ mouseLatitude NOTIFY mouseCoordinateChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY renderingChanged)
    Q_PROPERTY(bool rectangleZoomActive READ rectangleZoomActive
               WRITE setRectangleZoomActive NOTIFY rectangleZoomActiveChanged)
    Q_PROPERTY(QString inspectionMode READ inspectionMode
               WRITE setInspectionMode NOTIFY inspectionModeChanged)

public:
    explicit MapCanvas(QQuickItem *parent = nullptr);
    ~MapCanvas() override;

    void paint(QPainter *painter) override;

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

    Q_INVOKABLE void zoomBy(double delta);
    Q_INVOKABLE void panBy(double horizontalPixels, double verticalPixels);
    Q_INVOKABLE void fitBounds(double minLon, double minLat,
                               double maxLon, double maxLat);
    Q_INVOKABLE void setRectangleZoomActive(bool active);
    Q_INVOKABLE void setInspectionMode(const QString &mode);
    Q_INVOKABLE void setSelectedFeatureWkt(const QString &wkt);
    Q_INVOKABLE void clearSelectedFeature();
    Q_INVOKABLE void refresh();

signals:
    void layerModelChanged();
    void viewportChanged();
    void mouseCoordinateChanged();
    void renderingChanged();
    void rectangleZoomActiveChanged();
    void inspectionModeChanged();
    void mapClicked(double longitude, double latitude);
    void renderError(const QString &message);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
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
    bool m_selectingRectangle = false;
    bool m_rectangleZoomActive = false;
    bool m_rendering = false;
    QString m_inspectionMode = QStringLiteral("pan");
};
