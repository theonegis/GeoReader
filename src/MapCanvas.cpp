#include "MapCanvas.h"
#include "RasterRenderer.h"

#include <QCursor>
#include <QDebug>
#include <QFileInfo>
#include <QHoverEvent>
#include <QMouseEvent>
#include <QMutexLocker>
#include <QNetworkDiskCache>
#include <QNetworkReply>
#include <QPainter>
#include <QPainterPath>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QThread>
#include <QWheelEvent>
#include <QtConcurrent>

#include <gdal_priv.h>
#include <gdal_utils.h>
#include <cpl_vsi.h>
#include <ogrsf_frmts.h>

#include <mapnik/agg_renderer.hpp>
#include <mapnik/datasource_cache.hpp>
#include <mapnik/image.hpp>
#include <mapnik/image_util.hpp>
#include <mapnik/load_map.hpp>
#include <mapnik/map.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <utility>
#include <vector>

namespace {

constexpr double kMaxLatitude = 85.05112878;
constexpr double kEarthRadius = 6378137.0;
constexpr int kTileSize = 256;

QQuickItem *deepestItemAt(QQuickItem *root, const QPointF &scenePosition)
{
    QQuickItem *current = root;
    while (current) {
        const QPointF localPosition = current->mapFromScene(scenePosition);
        QQuickItem *child =
            current->childAt(localPosition.x(), localPosition.y());
        if (!child)
            break;
        current = child;
    }
    return current;
}

int maximumTileZoom(const QString &baseMap)
{
    if (baseMap == QStringLiteral("opentopomap"))
        return 17;
    if (baseMap == QStringLiteral("esri_imagery"))
        return 19;
    return 19;
}

QUrl tileUrl(const QString &baseMap, int zoom, int x, int y)
{
    if (baseMap == QStringLiteral("esri_imagery")) {
        return QUrl(QStringLiteral(
            "https://services.arcgisonline.com/ArcGIS/rest/services/"
            "World_Imagery/MapServer/tile/%1/%2/%3")
                        .arg(zoom).arg(y).arg(x));
    }
    if (baseMap == QStringLiteral("opentopomap")) {
        constexpr std::array<char, 3> subdomains {'a', 'b', 'c'};
        const char subdomain = subdomains.at(
            static_cast<std::size_t>((x + y) % subdomains.size()));
        return QUrl(QStringLiteral("https://%1.tile.opentopomap.org/%2/%3/%4.png")
                        .arg(QChar::fromLatin1(subdomain))
                        .arg(zoom).arg(x).arg(y));
    }
    return QUrl(QStringLiteral("https://tile.openstreetmap.org/%1/%2/%3.png")
                    .arg(zoom).arg(x).arg(y));
}

QString xmlEscaped(const QString &value)
{
    return value.toHtmlEscaped();
}

QString vectorDatasourceXml(const LayerSnapshot &layer)
{
    const QString extension = QFileInfo(layer.path).suffix().toLower();
    QString type;
    if (extension == QStringLiteral("shp"))
        type = QStringLiteral("shape");
    else if (extension == QStringLiteral("geojson") || extension == QStringLiteral("json"))
        type = QStringLiteral("geojson");
    else
        type = QStringLiteral("ogr");

    QString result = QStringLiteral(
        "<Parameter name=\"type\">%1</Parameter>"
        "<Parameter name=\"file\">%2</Parameter>")
        .arg(type, xmlEscaped(layer.path));
    if (type == QStringLiteral("ogr") && !layer.sourceLayer.isEmpty()) {
        result += QStringLiteral("<Parameter name=\"layer\">%1</Parameter>")
                      .arg(xmlEscaped(layer.sourceLayer));
    }
    return result;
}

double bandMinimum(const LayerSnapshot &layer, int band);
double bandMaximum(const LayerSnapshot &layer, int band);

QStringList colorRampColors(const QString &name)
{
    static const QHash<QString, QStringList> ramps {
        {QStringLiteral("Viridis"),
         {QStringLiteral("#440154"), QStringLiteral("#3b528b"),
          QStringLiteral("#21918c"), QStringLiteral("#5ec962"),
          QStringLiteral("#fde725")}},
        {QStringLiteral("Plasma"),
         {QStringLiteral("#0d0887"), QStringLiteral("#7e03a8"),
          QStringLiteral("#cc4778"), QStringLiteral("#f89540"),
          QStringLiteral("#f0f921")}},
        {QStringLiteral("Inferno"),
         {QStringLiteral("#000004"), QStringLiteral("#420a68"),
          QStringLiteral("#932667"), QStringLiteral("#dd513a"),
          QStringLiteral("#fca50a"), QStringLiteral("#fcffa4")}},
        {QStringLiteral("Magma"),
         {QStringLiteral("#000004"), QStringLiteral("#51127c"),
          QStringLiteral("#b73779"), QStringLiteral("#fc8961"),
          QStringLiteral("#fcfdbf")}},
        {QStringLiteral("Cividis"),
         {QStringLiteral("#00224e"), QStringLiteral("#2e4a7d"),
          QStringLiteral("#576d8c"), QStringLiteral("#848e88"),
          QStringLiteral("#b7ad6f"), QStringLiteral("#fee838")}},
        {QStringLiteral("Turbo"),
         {QStringLiteral("#30123b"), QStringLiteral("#4666e5"),
          QStringLiteral("#28bbec"), QStringLiteral("#32f298"),
          QStringLiteral("#a4fc3c"), QStringLiteral("#f9ba38"),
          QStringLiteral("#e85d0b"), QStringLiteral("#7a0403")}},
        {QStringLiteral("Terrain"),
         {QStringLiteral("#1d4e89"), QStringLiteral("#5aa9e6"),
          QStringLiteral("#9fd356"), QStringLiteral("#a98467"),
          QStringLiteral("#f5f5f5")}},
        {QStringLiteral("Gray"),
         {QStringLiteral("#111111"), QStringLiteral("#f5f5f5")}}
    };
    return ramps.value(name, ramps.value(QStringLiteral("Viridis")));
}

QString colorReliefDefinition(const LayerSnapshot &layer)
{
    const QStringList colors = colorRampColors(layer.colorRamp);
    const double minimum = bandMinimum(layer, layer.grayBand);
    const double maximum = bandMaximum(layer, layer.grayBand);
    const double span = maximum - minimum;
    QString result;
    for (qsizetype index = 0; index < colors.size(); ++index) {
        const double fraction = colors.size() == 1
            ? 0.0
            : static_cast<double>(index)
                  / static_cast<double>(colors.size() - 1);
        const QColor color(colors.at(index));
        result += QStringLiteral("%1 %2 %3 %4 255\n")
                      .arg(QString::number(minimum + span * fraction, 'g', 16))
                      .arg(color.red()).arg(color.green()).arg(color.blue());
    }
    result += QStringLiteral("nv 0 0 0 0\n");
    return result;
}

class TemporaryVrt final
{
public:
    TemporaryVrt() = default;
    TemporaryVrt(const TemporaryVrt &) = delete;
    TemporaryVrt &operator=(const TemporaryVrt &) = delete;
    TemporaryVrt(TemporaryVrt &&other) noexcept
        : m_paths(std::exchange(other.m_paths, {})),
          m_displayPath(std::exchange(other.m_displayPath, {})) {}
    TemporaryVrt &operator=(TemporaryVrt &&other) noexcept
    {
        if (this != &other) {
            cleanup();
            m_paths = std::exchange(other.m_paths, {});
            m_displayPath = std::exchange(other.m_displayPath, {});
        }
        return *this;
    }
    ~TemporaryVrt() { cleanup(); }
    void addPath(QString path) { m_paths.push_back(std::move(path)); }
    void setDisplayPath(const QString &path) { m_displayPath = path; }
    const QString &path() const { return m_displayPath; }
    explicit operator bool() const { return !m_displayPath.isEmpty(); }

private:
    void cleanup()
    {
        for (auto it = m_paths.crbegin(); it != m_paths.crend(); ++it)
            VSIUnlink(it->toUtf8().constData());
    }
    QVector<QString> m_paths;
    QString m_displayPath;
};

using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

DatasetPtr translateToVrt(const QString &path, GDALDataset *source,
                          const QStringList &optionStrings)
{
    QVector<QByteArray> encoded;
    encoded.reserve(optionStrings.size());
    for (const auto &option : optionStrings)
        encoded.push_back(option.toUtf8());
    QVector<char *> arguments;
    arguments.reserve(encoded.size() + 1);
    for (auto &option : encoded)
        arguments.push_back(option.data());
    arguments.push_back(nullptr);

    std::unique_ptr<GDALTranslateOptions, decltype(&GDALTranslateOptionsFree)> options(
        GDALTranslateOptionsNew(arguments.data(), nullptr),
        GDALTranslateOptionsFree);
    if (!options)
        return {nullptr, GDALClose};

    int usageError = FALSE;
    return DatasetPtr(
        static_cast<GDALDataset *>(GDALTranslate(path.toUtf8().constData(),
                                                source, options.get(),
                                                &usageError)),
        GDALClose);
}

bool writeVsiFile(const QString &path, const QByteArray &contents)
{
    VSILFILE *file = VSIFOpenL(path.toUtf8().constData(), "wb");
    if (!file)
        return false;
    const bool written =
        VSIFWriteL(contents.constData(), 1,
                   static_cast<size_t>(contents.size()), file)
        == static_cast<size_t>(contents.size());
    VSIFCloseL(file);
    return written;
}

double bandMinimum(const LayerSnapshot &layer, int band)
{
    const int index = band - 1;
    return index >= 0 && index < layer.bandMinimums.size()
        ? layer.bandMinimums.at(index) : 0.0;
}

double bandMaximum(const LayerSnapshot &layer, int band)
{
    const int index = band - 1;
    return index >= 0 && index < layer.bandMaximums.size()
        ? layer.bandMaximums.at(index) : 255.0;
}

double parsedNoData(const QString &text)
{
    return text.compare(QStringLiteral("nan"), Qt::CaseInsensitive) == 0
        ? std::numeric_limits<double>::quiet_NaN()
        : text.toDouble();
}

TemporaryVrt createDisplayVrt(const LayerSnapshot &layer, quint64 generation)
{
    DatasetPtr source(
        static_cast<GDALDataset *>(GDALOpenEx(layer.path.toUtf8().constData(),
                                              GDAL_OF_RASTER | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    if (!source)
        return {};

    TemporaryVrt result;
    const QString pathPrefix = QStringLiteral("/vsimem/georeader_%1_%2")
                                   .arg(layer.id).arg(generation);
    const QVector<int> selectedBands =
        layer.rasterMode == QStringLiteral("single")
        ? QVector<int> {layer.grayBand}
        : QVector<int> {layer.redBand, layer.greenBand, layer.blueBand};

    const QString selectionPath = pathPrefix + QStringLiteral("_source.vrt");
    result.addPath(selectionPath);
    QStringList selectionOptions {QStringLiteral("-of"), QStringLiteral("VRT")};
    const auto appendBand = [&selectionOptions](int band) {
        selectionOptions << QStringLiteral("-b") << QString::number(band);
    };
    for (const int band : selectedBands)
        appendBand(band);
    DatasetPtr selection =
        translateToVrt(selectionPath, source.get(), selectionOptions);
    if (!selection)
        return {};

    for (int outputBand = 1; outputBand <= selection->GetRasterCount();
         ++outputBand) {
        GDALRasterBand *band = selection->GetRasterBand(outputBand);
        if (!band)
            continue;
        if (layer.noDataEnabled)
            band->SetNoDataValue(parsedNoData(layer.noDataValue));
        else
            band->DeleteNoDataValue();
    }

    if (layer.rasterMode == QStringLiteral("single")) {
        const QString colorPath =
            pathPrefix + QStringLiteral("_colors.txt");
        const QString displayPath =
            pathPrefix + QStringLiteral("_colorized.vrt");
        result.addPath(colorPath);
        result.addPath(displayPath);
        if (!writeVsiFile(colorPath,
                          colorReliefDefinition(layer).toUtf8()))
            return {};

        QStringList demStrings {
            QStringLiteral("-of"), QStringLiteral("VRT"),
            QStringLiteral("-alpha")
        };
        QVector<QByteArray> encoded;
        for (const auto &option : demStrings)
            encoded.push_back(option.toUtf8());
        QVector<char *> arguments;
        for (auto &option : encoded)
            arguments.push_back(option.data());
        arguments.push_back(nullptr);
        std::unique_ptr<GDALDEMProcessingOptions,
                        decltype(&GDALDEMProcessingOptionsFree)> options(
            GDALDEMProcessingOptionsNew(arguments.data(), nullptr),
            GDALDEMProcessingOptionsFree);
        int usageError = FALSE;
        DatasetPtr colorized(
            static_cast<GDALDataset *>(
                GDALDEMProcessing(displayPath.toUtf8().constData(),
                                  selection.get(), "color-relief",
                                  colorPath.toUtf8().constData(),
                                  options.get(), &usageError)),
            GDALClose);
        if (!colorized || usageError)
            return {};
        colorized.reset();
        selection.reset();
        result.setDisplayPath(displayPath);
        return result;
    }

    GDALDataset *displaySource = selection.get();
    DatasetPtr alphaVrt(nullptr, GDALClose);
    if (layer.noDataEnabled) {
        const QString alphaPath = pathPrefix + QStringLiteral("_alpha.vrt");
        result.addPath(alphaPath);
        QStringList warpStrings {
            QStringLiteral("-of"), QStringLiteral("VRT"),
            QStringLiteral("-srcnodata"), layer.noDataValue,
            QStringLiteral("-dstalpha")
        };
        QVector<QByteArray> encoded;
        for (const auto &option : warpStrings)
            encoded.push_back(option.toUtf8());
        QVector<char *> arguments;
        for (auto &option : encoded)
            arguments.push_back(option.data());
        arguments.push_back(nullptr);
        std::unique_ptr<GDALWarpAppOptions,
                        decltype(&GDALWarpAppOptionsFree)> options(
            GDALWarpAppOptionsNew(arguments.data(), nullptr),
            GDALWarpAppOptionsFree);
        GDALDatasetH sourceHandle = selection.get();
        int usageError = FALSE;
        alphaVrt = DatasetPtr(
            static_cast<GDALDataset *>(
                GDALWarp(alphaPath.toUtf8().constData(), nullptr, 1,
                         &sourceHandle, options.get(), &usageError)),
            GDALClose);
        if (!alphaVrt || usageError)
            return {};
        displaySource = alphaVrt.get();
    }

    const QString displayPath = pathPrefix + QStringLiteral("_display.vrt");
    result.addPath(displayPath);
    QStringList displayOptions {
        QStringLiteral("-of"), QStringLiteral("VRT"),
        QStringLiteral("-ot"), QStringLiteral("Byte")
    };
    for (int band = 1; band <= displaySource->GetRasterCount(); ++band)
        displayOptions << QStringLiteral("-b") << QString::number(band);
    for (int outputBand = 1; outputBand <= 3; ++outputBand) {
        const int sourceBand = selectedBands.at(outputBand - 1);
        displayOptions
            << QStringLiteral("-scale_%1").arg(outputBand)
            << QString::number(bandMinimum(layer, sourceBand), 'g', 16)
            << QString::number(bandMaximum(layer, sourceBand), 'g', 16)
            << QStringLiteral("0") << QStringLiteral("255");
    }
    DatasetPtr display =
        translateToVrt(displayPath, displaySource, displayOptions);
    if (!display)
        return {};
    display.reset();
    alphaVrt.reset();
    selection.reset();
    result.setDisplayPath(displayPath);
    return result;
}

} // namespace

MapCanvas::MapCanvas(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    setAcceptedMouseButtons(Qt::LeftButton);
    setAcceptHoverEvents(true);
    setAntialiasing(false);
    setMipmap(false);
    setOpaquePainting(true);
    updateCursor();

    auto *diskCache = new QNetworkDiskCache(&m_network);
    diskCache->setCacheDirectory(
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
        + QStringLiteral("/basemaps"));
    diskCache->setMaximumCacheSize(256LL * 1024LL * 1024LL);
    m_network.setCache(diskCache);
    connect(&m_network, &QNetworkAccessManager::finished,
            this, &MapCanvas::tileFinished);

    m_renderTimer.setSingleShot(true);
    m_renderTimer.setInterval(90);
    connect(&m_renderTimer, &QTimer::timeout,
            this, &MapCanvas::beginOverlayRender);
    connect(&m_renderWatcher, &QFutureWatcher<RenderResult>::finished, this, [this] {
        const RenderResult result = m_renderWatcher.result();
        if (result.generation == m_generation) {
            {
                QMutexLocker locker(&m_imageMutex);
                m_overlay = result.image;
                m_overlayViewport = result.viewport;
            }
            update();
            if (!result.error.isEmpty()) {
                qWarning().noquote() << "Map rendering failed:" << result.error;
                emit renderError(result.error);
            }
        }
        setRendering(false);
        if (result.generation != m_generation)
            m_renderTimer.start();
    });
}

MapCanvas::~MapCanvas()
{
    m_renderWatcher.cancel();
    m_renderWatcher.waitForFinished();
}

void MapCanvas::setLayerModel(LayerModel *model)
{
    if (m_layerModel == model)
        return;
    if (m_layerModel)
        disconnect(m_layerModel, nullptr, this, nullptr);
    m_layerModel = model;
    if (m_layerModel) {
        connect(m_layerModel, &LayerModel::renderingChanged,
                this, &MapCanvas::scheduleOverlayRender);
        connect(m_layerModel, &QAbstractItemModel::rowsInserted,
                this, &MapCanvas::scheduleOverlayRender);
        connect(m_layerModel, &QAbstractItemModel::rowsRemoved,
                this, &MapCanvas::scheduleOverlayRender);
    }
    emit layerModelChanged();
    scheduleOverlayRender();
}

QString MapCanvas::baseMapAttribution() const
{
    if (m_baseMap == QStringLiteral("esri_imagery")) {
        return tr("Tiles © Esri — Esri, Maxar, Earthstar Geographics, "
                  "and the GIS User Community");
    }
    if (m_baseMap == QStringLiteral("opentopomap")) {
        return tr("Map data © OpenStreetMap contributors, SRTM · "
                  "Map style © OpenTopoMap (CC-BY-SA)");
    }
    return tr("© OpenStreetMap contributors");
}

void MapCanvas::setBaseMap(const QString &baseMap)
{
    static const QSet<QString> supported {
        QStringLiteral("osm"),
        QStringLiteral("esri_imagery"),
        QStringLiteral("opentopomap")
    };
    const QString normalized = supported.contains(baseMap)
        ? baseMap : QStringLiteral("osm");
    if (m_baseMap == normalized)
        return;

    m_baseMap = normalized;
    {
        QMutexLocker locker(&m_imageMutex);
        m_tiles.clear();
    }
    emit baseMapChanged();
    update();
}

QPointF MapCanvas::lonLatToWorld(double longitude, double latitude, double zoom)
{
    latitude = std::clamp(latitude, -kMaxLatitude, kMaxLatitude);
    const double scale = kTileSize * std::exp2(zoom);
    const double x = (longitude + 180.0) / 360.0 * scale;
    const double latitudeRadians = latitude * std::numbers::pi / 180.0;
    const double y = (1.0 - std::asinh(std::tan(latitudeRadians))
                             / std::numbers::pi) / 2.0 * scale;
    return {x, y};
}

QPointF MapCanvas::worldToLonLat(double x, double y, double zoom)
{
    const double scale = kTileSize * std::exp2(zoom);
    const double longitude = x / scale * 360.0 - 180.0;
    const double n = std::numbers::pi * (1.0 - 2.0 * y / scale);
    const double latitude = std::atan(std::sinh(n)) * 180.0 / std::numbers::pi;
    return {longitude, latitude};
}

QPointF MapCanvas::lonLatToMercator(double longitude, double latitude)
{
    latitude = std::clamp(latitude, -kMaxLatitude, kMaxLatitude);
    const double x = kEarthRadius * longitude * std::numbers::pi / 180.0;
    const double radians = latitude * std::numbers::pi / 180.0;
    const double y = kEarthRadius * std::log(std::tan(std::numbers::pi / 4.0
                                                     + radians / 2.0));
    return {x, y};
}

QPointF MapCanvas::screenToLonLat(const QPointF &screenPoint) const
{
    const QPointF center = lonLatToWorld(m_centerLongitude, m_centerLatitude,
                                         m_zoomLevel);
    return worldToLonLat(center.x() + screenPoint.x() - width() / 2.0,
                         center.y() + screenPoint.y() - height() / 2.0,
                         m_zoomLevel);
}

MapViewport MapCanvas::currentViewport() const
{
    const QPointF topLeft = screenToLonLat({0.0, 0.0});
    const QPointF bottomRight = screenToLonLat({width(), height()});
    const QPointF min = lonLatToMercator(topLeft.x(), bottomRight.y());
    const QPointF max = lonLatToMercator(bottomRight.x(), topLeft.y());
    return {
        std::max(1, static_cast<int>(std::round(width()))),
        std::max(1, static_cast<int>(std::round(height()))),
        min.x(), min.y(), max.x(), max.y()
    };
}

void MapCanvas::paint(QPainter *painter)
{
    QImage overlay;
    MapViewport overlayViewport;
    {
        QMutexLocker locker(&m_imageMutex);
        overlay = m_overlay;
        overlayViewport = m_overlayViewport;
    }

    painter->fillRect(boundingRect(), QColor(QStringLiteral("#E8EDF2")));
    drawBaseMap(painter);
    if (!overlay.isNull() && overlayViewport.isValid()) {
        const MapViewport viewport = currentViewport();
        const double horizontalSpan =
            viewport.maxMercatorX - viewport.minMercatorX;
        const double verticalSpan =
            viewport.maxMercatorY - viewport.minMercatorY;
        const QRectF targetRectangle(
            (overlayViewport.minMercatorX - viewport.minMercatorX)
                / horizontalSpan * width(),
            (viewport.maxMercatorY - overlayViewport.maxMercatorY)
                / verticalSpan * height(),
            (overlayViewport.maxMercatorX - overlayViewport.minMercatorX)
                / horizontalSpan * width(),
            (overlayViewport.maxMercatorY - overlayViewport.minMercatorY)
                / verticalSpan * height());
        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(targetRectangle, overlay);
    }

    drawSelectedFeature(painter);

    if (m_rectangleZoomActive && !m_selectionRectangle.isNull()) {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing, true);
        painter->setPen(QPen(QColor(QStringLiteral("#2475E9")), 1.5,
                             Qt::DashLine));
        painter->setBrush(QColor(36, 117, 233, 36));
        painter->drawRoundedRect(m_selectionRectangle.normalized(), 3.0, 3.0);
        painter->restore();
    }
}

void MapCanvas::drawBaseMap(QPainter *painter)
{
    const int tileZoom = std::clamp(
        static_cast<int>(std::floor(m_zoomLevel)), 0,
        maximumTileZoom(m_baseMap));
    const double fractionalScale = std::exp2(m_zoomLevel - tileZoom);
    const QPointF centerAtTileZoom =
        lonLatToWorld(m_centerLongitude, m_centerLatitude, tileZoom);
    const QPointF topLeftAtTileZoom(
        centerAtTileZoom.x() - width() / (2.0 * fractionalScale),
        centerAtTileZoom.y() - height() / (2.0 * fractionalScale));
    const int minTileX = static_cast<int>(std::floor(topLeftAtTileZoom.x() / kTileSize));
    const int minTileY = static_cast<int>(std::floor(topLeftAtTileZoom.y() / kTileSize));
    const int maxTileX = static_cast<int>(std::floor(
        (topLeftAtTileZoom.x() + width() / fractionalScale) / kTileSize));
    const int maxTileY = static_cast<int>(std::floor(
        (topLeftAtTileZoom.y() + height() / fractionalScale) / kTileSize));
    const int tileCount = 1 << tileZoom;

    painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
    for (int tileY = minTileY; tileY <= maxTileY; ++tileY) {
        if (tileY < 0 || tileY >= tileCount)
            continue;
        for (int tileX = minTileX; tileX <= maxTileX; ++tileX) {
            const int wrappedX = (tileX % tileCount + tileCount) % tileCount;
            const QString key = QStringLiteral("%1/%2/%3/%4")
                                    .arg(m_baseMap)
                                    .arg(tileZoom).arg(wrappedX).arg(tileY);
            const QRectF target(
                (tileX * kTileSize - topLeftAtTileZoom.x()) * fractionalScale,
                (tileY * kTileSize - topLeftAtTileZoom.y()) * fractionalScale,
                kTileSize * fractionalScale + 0.5,
                kTileSize * fractionalScale + 0.5);
            if (const auto it = m_tiles.constFind(key); it != m_tiles.cend())
                painter->drawImage(target, *it);
            else {
                painter->fillRect(target, QColor(QStringLiteral("#E8EDF2")));
                requestTile(tileZoom, wrappedX, tileY, key);
            }
        }
    }
}

void MapCanvas::requestTile(int zoom, int x, int y, const QString &key)
{
    if (QThread::currentThread() != thread()) {
        QMetaObject::invokeMethod(
            this,
            [this, zoom, x, y, key] { requestTile(zoom, x, y, key); },
            Qt::QueuedConnection);
        return;
    }
    if (m_pendingTiles.contains(key))
        return;
    m_pendingTiles.insert(key);
    const QUrl url = tileUrl(m_baseMap, zoom, x, y);
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::UserAgentHeader,
                      QStringLiteral("GeoReader/%1 (+https://github.com/"
                                     "theonegis/GeoReader)")
                          .arg(QString::fromLatin1(GEOREADER_VERSION)));
    request.setAttribute(QNetworkRequest::CacheLoadControlAttribute,
                         QNetworkRequest::PreferCache);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply *reply = m_network.get(request);
    reply->setProperty("tileKey", key);
}

void MapCanvas::tileFinished(QNetworkReply *reply)
{
    const QString key = reply->property("tileKey").toString();
    m_pendingTiles.remove(key);
    if (reply->error() == QNetworkReply::NoError) {
        QImage image;
        if (image.loadFromData(reply->readAll())) {
            QMutexLocker locker(&m_imageMutex);
            if (m_tiles.size() > 512)
                m_tiles.clear();
            m_tiles.insert(key, std::move(image));
            update();
        }
    }
    reply->deleteLater();
}

void MapCanvas::zoomBy(double delta)
{
    const double nextZoom = std::clamp(m_zoomLevel + delta, 1.0, 20.0);
    if (qFuzzyCompare(nextZoom, m_zoomLevel))
        return;
    m_zoomLevel = nextZoom;
    emit viewportChanged();
    update();
    scheduleOverlayRender();
}

void MapCanvas::panBy(double horizontalPixels, double verticalPixels)
{
    const QPointF center = lonLatToWorld(m_centerLongitude, m_centerLatitude,
                                         m_zoomLevel);
    const QPointF coordinate = worldToLonLat(center.x() - horizontalPixels,
                                             center.y() - verticalPixels,
                                             m_zoomLevel);
    m_centerLongitude = coordinate.x();
    m_centerLatitude = std::clamp(coordinate.y(), -kMaxLatitude, kMaxLatitude);
    emit viewportChanged();
    update();
    scheduleOverlayRender();
}

void MapCanvas::fitBounds(double minLon, double minLat, double maxLon, double maxLat)
{
    if (!std::isfinite(minLon) || !std::isfinite(minLat)
        || !std::isfinite(maxLon) || !std::isfinite(maxLat)
        || minLon > maxLon || minLat > maxLat
        || width() <= 0 || height() <= 0) {
        return;
    }
    // 点图层或严格水平/垂直的线可能只有零宽或零高范围；为其补充一个
    // 很小的地理范围，确保“缩放至图层”仍然可用。
    if (qFuzzyCompare(minLon, maxLon)) {
        minLon -= 0.005;
        maxLon += 0.005;
    }
    if (qFuzzyCompare(minLat, maxLat)) {
        minLat -= 0.005;
        maxLat += 0.005;
    }
    minLat = std::clamp(minLat, -kMaxLatitude, kMaxLatitude);
    maxLat = std::clamp(maxLat, -kMaxLatitude, kMaxLatitude);
    m_centerLongitude = (minLon + maxLon) / 2.0;
    m_centerLatitude = (minLat + maxLat) / 2.0;

    const QPointF topLeft = lonLatToWorld(minLon, maxLat, 0.0);
    const QPointF bottomRight = lonLatToWorld(maxLon, minLat, 0.0);
    const double availableWidth = std::max(1.0, width() - 72.0);
    const double availableHeight = std::max(1.0, height() - 72.0);
    const double scaleX = availableWidth / std::max(1e-9, bottomRight.x() - topLeft.x());
    const double scaleY = availableHeight / std::max(1e-9, bottomRight.y() - topLeft.y());
    m_zoomLevel = std::clamp(std::log2(std::min(scaleX, scaleY)), 1.0, 20.0);
    emit viewportChanged();
    update();
    scheduleOverlayRender();
}

void MapCanvas::setRectangleZoomActive(bool active)
{
    if (m_rectangleZoomActive == active)
        return;

    m_rectangleZoomActive = active;
    m_selectingRectangle = false;
    m_dragging = false;
    m_selectionRectangle = {};
    updateCursor();
    emit rectangleZoomActiveChanged();
    update();
}

void MapCanvas::setInspectionMode(const QString &mode)
{
    const auto normalized =
        mode == QStringLiteral("vector") || mode == QStringLiteral("raster")
        ? mode
        : QStringLiteral("pan");
    if (m_inspectionMode == normalized)
        return;

    m_inspectionMode = normalized;
    m_dragging = false;
    updateCursor();
    emit inspectionModeChanged();
}

void MapCanvas::setSelectedFeatureWkt(const QString &wkt)
{
    if (m_selectedFeatureWkt == wkt)
        return;
    m_selectedFeatureWkt = wkt;
    update();
}

void MapCanvas::clearSelectedFeature()
{
    setSelectedFeatureWkt({});
}

void MapCanvas::refresh()
{
    update();
    scheduleOverlayRender();
}

void MapCanvas::drawSelectedFeature(QPainter *painter) const
{
    if (m_selectedFeatureWkt.isEmpty())
        return;

    const auto [geometry, error] =
        OGRGeometryFactory::createFromWkt(m_selectedFeatureWkt.toUtf8().constData());
    if (error != OGRERR_NONE || !geometry)
        return;

    const QPointF center =
        lonLatToWorld(m_centerLongitude, m_centerLatitude, m_zoomLevel);
    const auto screenPoint = [this, center](double longitude, double latitude) {
        const QPointF world = lonLatToWorld(longitude, latitude, m_zoomLevel);
        return QPointF(world.x() - center.x() + width() / 2.0,
                       world.y() - center.y() + height() / 2.0);
    };

    QPainterPath path;
    path.setFillRule(Qt::OddEvenFill);
    const auto appendLine = [&path, &screenPoint](const OGRLineString *line,
                                                  bool close) {
        if (!line || line->getNumPoints() == 0)
            return;
        path.moveTo(screenPoint(line->getX(0), line->getY(0)));
        for (int point = 1; point < line->getNumPoints(); ++point)
            path.lineTo(screenPoint(line->getX(point), line->getY(point)));
        if (close)
            path.closeSubpath();
    };

    const auto appendGeometry =
        [&path, &screenPoint, &appendLine](auto &&self,
                                           const OGRGeometry *part) -> void {
        if (!part)
            return;
        if (const auto *point = dynamic_cast<const OGRPoint *>(part)) {
            const QPointF centerPoint = screenPoint(point->getX(), point->getY());
            path.addEllipse(centerPoint, 6.5, 6.5);
            return;
        }
        if (const auto *polygon = dynamic_cast<const OGRPolygon *>(part)) {
            appendLine(polygon->getExteriorRing(), true);
            for (int ring = 0; ring < polygon->getNumInteriorRings(); ++ring)
                appendLine(polygon->getInteriorRing(ring), true);
            return;
        }
        if (const auto *line = dynamic_cast<const OGRLineString *>(part)) {
            appendLine(line, dynamic_cast<const OGRLinearRing *>(line) != nullptr);
            return;
        }
        if (const auto *collection =
                dynamic_cast<const OGRGeometryCollection *>(part)) {
            for (int index = 0; index < collection->getNumGeometries(); ++index)
                self(self, collection->getGeometryRef(index));
            return;
        }

        std::unique_ptr<OGRGeometry> linear(part->getLinearGeometry());
        if (linear && linear->getGeometryType() != part->getGeometryType())
            self(self, linear.get());
    };
    appendGeometry(appendGeometry, geometry.get());
    if (path.isEmpty())
        return;

    const OGRwkbGeometryType geometryType =
        wkbFlatten(geometry->getGeometryType());
    const bool fillGeometry =
        geometryType == wkbPoint || geometryType == wkbMultiPoint
        || geometryType == wkbPolygon || geometryType == wkbMultiPolygon
        || geometryType == wkbCurvePolygon
        || geometryType == wkbMultiSurface;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setBrush(fillGeometry ? QBrush(QColor(255, 196, 0, 54))
                                   : Qt::NoBrush);
    painter->setPen(QPen(QColor(255, 255, 255, 230), 7.0,
                         Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(path);
    painter->setBrush(fillGeometry ? QBrush(QColor(255, 196, 0, 72))
                                   : Qt::NoBrush);
    painter->setPen(QPen(QColor(QStringLiteral("#FFB300")), 3.5,
                         Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter->drawPath(path);
    painter->restore();
}

void MapCanvas::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size())
        scheduleOverlayRender();
}

void MapCanvas::mousePressEvent(QMouseEvent *event)
{
    m_pressPosition = event->position();
    m_lastMousePosition = event->position();
    m_dragging = false;

    if (m_rectangleZoomActive) {
        m_selectingRectangle = true;
        m_selectionRectangle =
            QRectF(event->position(), event->position());
        updateMouseCoordinate(event->position());
        update();
        event->accept();
        return;
    }

    if (m_inspectionMode != QStringLiteral("pan")) {
        updateMouseCoordinate(event->position());
        event->accept();
        return;
    }

    setCursor(Qt::ClosedHandCursor);
    event->accept();
}

void MapCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_selectingRectangle) {
        m_selectionRectangle =
            QRectF(m_pressPosition, event->position()).normalized();
        updateMouseCoordinate(event->position());
        update();
        event->accept();
        return;
    }

    if (m_inspectionMode != QStringLiteral("pan")) {
        updateMouseCoordinate(event->position());
        event->accept();
        return;
    }

    const QPointF delta = event->position() - m_lastMousePosition;
    if (!m_dragging && (event->position() - m_pressPosition).manhattanLength() > 3.0)
        m_dragging = true;
    if (m_dragging)
        panBy(delta.x(), delta.y());
    m_lastMousePosition = event->position();
    updateMouseCoordinate(event->position());
    event->accept();
}

void MapCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_selectingRectangle) {
        const QRectF selection =
            QRectF(m_pressPosition, event->position()).normalized();
        m_selectingRectangle = false;
        setRectangleZoomActive(false);

        if (selection.width() >= 6.0 && selection.height() >= 6.0) {
            const QPointF topLeft = screenToLonLat(selection.topLeft());
            const QPointF bottomRight =
                screenToLonLat(selection.bottomRight());
            fitBounds(std::min(topLeft.x(), bottomRight.x()),
                      std::min(topLeft.y(), bottomRight.y()),
                      std::max(topLeft.x(), bottomRight.x()),
                      std::max(topLeft.y(), bottomRight.y()));
        }
        event->accept();
        return;
    }

    updateCursor();
    if (!m_dragging) {
        const QPointF coordinate = screenToLonLat(event->position());
        emit mapClicked(coordinate.x(), coordinate.y());
    }
    event->accept();
}

void MapCanvas::hoverMoveEvent(QHoverEvent *event)
{
    updateMouseCoordinate(event->position());
    event->accept();
}

void MapCanvas::wheelEvent(QWheelEvent *event)
{
    // 滚轮事件可能在 TableView/ScrollView 到达边界后继续传递到下层
    // QQuickItem。只有鼠标位置最上层的可视项属于 MapCanvas 时才缩放，
    // 从源头避免悬浮面板、属性表和工具栏上的滚轮影响地图。
    if (!isTopmostMapItemAt(event->position())) {
        event->ignore();
        return;
    }

    const double steps = event->angleDelta().y() / 120.0;
    if (!qFuzzyIsNull(steps))
        zoomBy(std::clamp(steps, -2.0, 2.0));
    event->accept();
}

bool MapCanvas::isTopmostMapItemAt(const QPointF &position) const
{
    QQuickWindow *quickWindow = window();
    QQuickItem *root = quickWindow ? quickWindow->contentItem() : nullptr;
    if (!root)
        return true;

    const QPointF scenePosition = mapToScene(position);
    QQuickItem *topmost = deepestItemAt(root, scenePosition);
    for (QQuickItem *item = topmost; item; item = item->parentItem()) {
        if (item == this)
            return true;
    }
    return false;
}

void MapCanvas::updateMouseCoordinate(const QPointF &position)
{
    const QPointF coordinate = screenToLonLat(position);
    m_mouseLongitude = coordinate.x();
    m_mouseLatitude = coordinate.y();
    emit mouseCoordinateChanged();
}

void MapCanvas::updateCursor()
{
    if (m_rectangleZoomActive || m_inspectionMode == QStringLiteral("raster")) {
        setCursor(Qt::CrossCursor);
        return;
    }
    if (m_inspectionMode == QStringLiteral("vector")) {
        setCursor(Qt::ArrowCursor);
        return;
    }
    setCursor(Qt::OpenHandCursor);
}

void MapCanvas::scheduleOverlayRender()
{
    ++m_generation;
    m_renderTimer.start();
}

void MapCanvas::beginOverlayRender()
{
    if (!m_layerModel || width() < 2 || height() < 2)
        return;
    if (m_renderWatcher.isRunning())
        return;
    const auto layers = m_layerModel->snapshots();
    const auto viewport = currentViewport();
    const auto generation = m_generation;
    setRendering(true);
    m_renderWatcher.setFuture(QtConcurrent::run(
        [layers, viewport, generation] {
            return renderLayers(layers, viewport, generation);
        }));
}

RenderResult MapCanvas::renderLayers(QVector<LayerSnapshot> layers,
                                     MapViewport viewport, quint64 generation)
{
    RenderResult result;
    result.generation = generation;
    result.viewport = viewport;
    if (layers.empty())
        return result;

    try {
        result.image = QImage(viewport.width, viewport.height,
                              QImage::Format_ARGB32_Premultiplied);
        result.image.fill(Qt::transparent);
        // 图层面板第 0 行代表最上层，因此必须从模型末尾向前绘制。
        // 每个矢量图层在自身位置独立离屏渲染，才能保留矢量/栅格交错顺序。
        for (auto iterator = layers.crbegin(); iterator != layers.crend();
             ++iterator) {
            const LayerSnapshot &layer = *iterator;
            if (!layer.visible)
                continue;
            if (layer.type == QStringLiteral("vector")) {
                const QString styleName = QStringLiteral("layer_style");
                const QString opacity =
                    QString::number(layer.opacity, 'f', 3);
                QString symbolizers;
                if (layer.geometryType == QStringLiteral("polygon")) {
                    symbolizers += QStringLiteral(
                        "<PolygonSymbolizer fill=\"%1\" "
                        "fill-opacity=\"%2\"/>")
                        .arg(layer.fillColor.name(QColor::HexRgb), opacity);
                }
                if (layer.geometryType == QStringLiteral("point")) {
                    symbolizers += QStringLiteral(
                        "<DotSymbolizer fill=\"%1\" opacity=\"%2\" "
                        "width=\"8\" height=\"8\"/>")
                        .arg(layer.fillColor.name(QColor::HexRgb), opacity);
                } else {
                    symbolizers += QStringLiteral(
                        "<LineSymbolizer stroke=\"%1\" stroke-width=\"%2\" "
                        "stroke-opacity=\"%3\" stroke-linecap=\"round\" "
                        "stroke-linejoin=\"round\"/>")
                        .arg(layer.lineColor.name(QColor::HexRgb),
                             QString::number(layer.lineWidth, 'f', 2),
                             opacity);
                }
                const QString style = QStringLiteral(
                    "<Style name=\"%1\"><Rule>%2</Rule></Style>")
                    .arg(styleName, symbolizers);
                const QString mapLayer = QStringLiteral(
                    "<Layer name=\"%1\" srs=\"%2\">"
                    "<StyleName>%3</StyleName><Datasource>%4</Datasource></Layer>")
                    .arg(xmlEscaped(layer.name), xmlEscaped(layer.srs), styleName,
                         vectorDatasourceXml(layer));
                const QString xml = QStringLiteral(
                    "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
                    "<Map srs=\"+proj=merc +a=6378137 +b=6378137 "
                    "+lat_ts=0 +lon_0=0 +x_0=0 +y_0=0 +k=1 +units=m "
                    "+nadgrids=@null +wktext +no_defs\" "
                    "background-color=\"transparent\">%1%2</Map>")
                                        .arg(style, mapLayer);
                mapnik::Map map(viewport.width, viewport.height);
                mapnik::load_map_string(map, xml.toStdString(), true);
                map.zoom_to_box(mapnik::box2d<double>(
                    viewport.minMercatorX, viewport.minMercatorY,
                    viewport.maxMercatorX, viewport.maxMercatorY));
                mapnik::image_rgba8 image(viewport.width, viewport.height);
                mapnik::agg_renderer<mapnik::image_rgba8> renderer(map, image);
                renderer.apply();
                const std::string png =
                    mapnik::save_to_string(image, "png");
                const QImage vectorImage = QImage::fromData(
                    reinterpret_cast<const uchar *>(png.data()),
                    static_cast<qsizetype>(png.size()), "PNG");
                QPainter painter(&result.image);
                painter.setCompositionMode(
                    QPainter::CompositionMode_SourceOver);
                painter.drawImage(QPoint(0, 0), vectorImage);
            } else if (layer.type == QStringLiteral("raster")) {
                const auto raster = renderRasterLayer(
                    layer,
                    {viewport.width, viewport.height,
                     viewport.minMercatorX, viewport.minMercatorY,
                     viewport.maxMercatorX, viewport.maxMercatorY});
                if (!raster.error.isEmpty() && result.error.isEmpty())
                    result.error = raster.error;
                if (!raster.image.isNull()) {
                    QPainter painter(&result.image);
                    painter.setCompositionMode(
                        QPainter::CompositionMode_SourceOver);
                    painter.drawImage(QPoint(0, 0), raster.image);
                }
            }
        }
    } catch (const std::exception &exception) {
        result.error = QString::fromUtf8(exception.what());
    }
    return result;
}

void MapCanvas::setRendering(bool rendering)
{
    if (m_rendering == rendering)
        return;
    m_rendering = rendering;
    emit renderingChanged();
}
