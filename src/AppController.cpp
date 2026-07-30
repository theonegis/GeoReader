#include "AppController.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QFutureWatcher>
#include <QLocale>
#include <QPointer>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QVector>
#include <QtConcurrent>

#include <gdal.h>
#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>

namespace {

using GdalDatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
struct OgrTransformDeleter {
    void operator()(OGRCoordinateTransformation *transform) const noexcept
    {
        OGRCoordinateTransformation::DestroyCT(transform);
    }
};
using OgrTransformPtr = std::unique_ptr<OGRCoordinateTransformation,
                                        OgrTransformDeleter>;

constexpr auto kOrganization = "GeoReader";
constexpr auto kApplication = "GeoReader";

const QHash<QString, QString> kDefaultShortcuts {
    {QStringLiteral("open"), QStringLiteral("Ctrl+O")},
    {QStringLiteral("zoomIn"), QStringLiteral("+")},
    {QStringLiteral("zoomOut"), QStringLiteral("-")},
    {QStringLiteral("pan"), QStringLiteral("Space")},
    {QStringLiteral("fit"), QStringLiteral("Ctrl+0")}
};

QString platformDefaultStyle()
{
    return QStringLiteral("FluentWinUI3");
}

QString systemDefaultLanguage()
{
    return QLocale::system().language() == QLocale::Chinese
        ? QStringLiteral("zh_CN")
        : QStringLiteral("en_US");
}

QString srsDisplayName(const OGRSpatialReference *srs)
{
    if (!srs)
        return {};
    auto copy = std::unique_ptr<OGRSpatialReference>(srs->Clone());
    copy->AutoIdentifyEPSG();
    const char *authority = copy->GetAuthorityName(nullptr);
    const char *code = copy->GetAuthorityCode(nullptr);
    if (authority && code)
        return QStringLiteral("%1:%2").arg(QString::fromUtf8(authority),
                                           QString::fromUtf8(code));
    const char *name = copy->GetName();
    return name ? QString::fromUtf8(name) : QString();
}

QString srsProjString(const OGRSpatialReference *srs)
{
    if (!srs)
        return QStringLiteral("+proj=longlat +datum=WGS84 +no_defs");
    auto copy = std::unique_ptr<OGRSpatialReference>(srs->Clone());
    char *proj = nullptr;
    if (copy->exportToProj4(&proj) != OGRERR_NONE || !proj)
        return QStringLiteral("+proj=longlat +datum=WGS84 +no_defs");
    const QString result = QString::fromUtf8(proj);
    CPLFree(proj);
    return result;
}

QString vectorGeometryType(OGRLayer *layer)
{
    if (!layer)
        return QStringLiteral("unknown");

    OGRwkbGeometryType geometryType = wkbFlatten(layer->GetGeomType());
    if (geometryType == wkbUnknown) {
        layer->ResetReading();
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)> feature(
            layer->GetNextFeature(), OGRFeature::DestroyFeature);
        if (feature && feature->GetGeometryRef())
            geometryType = wkbFlatten(feature->GetGeometryRef()->getGeometryType());
        layer->ResetReading();
    }

    switch (geometryType) {
    case wkbPoint:
    case wkbMultiPoint:
        return QStringLiteral("point");
    case wkbLineString:
    case wkbMultiLineString:
    case wkbCircularString:
    case wkbCompoundCurve:
    case wkbMultiCurve:
        return QStringLiteral("line");
    case wkbPolygon:
    case wkbMultiPolygon:
    case wkbCurvePolygon:
    case wkbMultiSurface:
    case wkbPolyhedralSurface:
    case wkbTIN:
        return QStringLiteral("polygon");
    default:
        return QStringLiteral("unknown");
    }
}

bool extentToWgs84(const OGREnvelope &extent, const OGRSpatialReference *source,
                   double &minLon, double &minLat, double &maxLon, double &maxLat)
{
    double xs[] {extent.MinX, extent.MaxX, extent.MaxX, extent.MinX};
    double ys[] {extent.MinY, extent.MinY, extent.MaxY, extent.MaxY};

    if (source) {
        auto sourceClone = std::unique_ptr<OGRSpatialReference>(source->Clone());
        auto target = std::make_unique<OGRSpatialReference>();
        target->SetWellKnownGeogCS("WGS84");
        sourceClone->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        target->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
        OgrTransformPtr transform(
            OGRCreateCoordinateTransformation(sourceClone.get(), target.get()));
        if (!transform || !transform->Transform(4, xs, ys))
            return false;
    }

    minLon = *std::min_element(std::begin(xs), std::end(xs));
    maxLon = *std::max_element(std::begin(xs), std::end(xs));
    minLat = *std::min_element(std::begin(ys), std::end(ys));
    maxLat = *std::max_element(std::begin(ys), std::end(ys));
    return std::isfinite(minLon) && std::isfinite(minLat)
           && std::isfinite(maxLon) && std::isfinite(maxLat);
}

OgrTransformPtr wgs84To(const OGRSpatialReference *target)
{
    if (!target)
        return {};
    auto source = std::make_unique<OGRSpatialReference>();
    auto targetCopy = std::unique_ptr<OGRSpatialReference>(target->Clone());
    source->SetWellKnownGeogCS("WGS84");
    source->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    targetCopy->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return OgrTransformPtr(
        OGRCreateCoordinateTransformation(source.get(), targetCopy.get()));
}

QString fieldValue(OGRFeature *feature, int index)
{
    if (!feature || !feature->IsFieldSetAndNotNull(index))
        return QStringLiteral("—");
    return QString::fromUtf8(feature->GetFieldAsString(index));
}

std::pair<double, double> approximateBandRange(GDALRasterBand *band)
{
    if (!band)
        return {0.0, 1.0};

    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double standardDeviation = 0.0;
    const bool hasCachedStatistics =
        band->GetStatistics(TRUE, FALSE, &minimum, &maximum, &mean,
                            &standardDeviation) == CE_None
        && std::isfinite(minimum) && std::isfinite(maximum);

    if (!hasCachedStatistics) {
        constexpr int kSampleDimension = 128;
        const int sampleWidth =
            std::clamp(band->GetXSize(), 1, kSampleDimension);
        const int sampleHeight =
            std::clamp(band->GetYSize(), 1, kSampleDimension);
        QVector<double> sampleValues(
            static_cast<qsizetype>(sampleWidth) * sampleHeight);
        GDALRasterIOExtraArg arguments;
        INIT_RASTERIO_EXTRA_ARG(arguments);
        arguments.eResampleAlg = GRIORA_NearestNeighbour;

        const bool sampled =
            band->RasterIO(GF_Read, 0, 0, band->GetXSize(), band->GetYSize(),
                           sampleValues.data(), sampleWidth, sampleHeight,
                           GDT_Float64, 0, 0, &arguments) == CE_None;
        int hasNoData = FALSE;
        const double noData = band->GetNoDataValue(&hasNoData);
        minimum = std::numeric_limits<double>::infinity();
        maximum = -std::numeric_limits<double>::infinity();
        if (sampled) {
            for (const double value : sampleValues) {
                if (!std::isfinite(value)
                    || (hasNoData && value == noData)) {
                    continue;
                }
                minimum = std::min(minimum, value);
                maximum = std::max(maximum, value);
            }
        }

        if (!std::isfinite(minimum) || !std::isfinite(maximum)) {
            switch (band->GetRasterDataType()) {
            case GDT_Byte: return {0.0, 255.0};
            case GDT_UInt16: return {0.0, 65535.0};
            case GDT_Int16: return {-32768.0, 32767.0};
            case GDT_UInt32: return {0.0, 4294967295.0};
            case GDT_Int32: return {-2147483648.0, 2147483647.0};
            default: return {0.0, 1.0};
            }
        }
    }

    if (minimum >= maximum) {
        const double padding = std::max(1.0, std::abs(minimum) * 0.01);
        minimum -= padding;
        maximum += padding;
    }
    return {minimum, maximum};
}

std::pair<double, double> dataTypeRange(GDALDataType type)
{
    switch (type) {
    case GDT_Byte: return {0.0, 255.0};
    case GDT_UInt16: return {0.0, 65535.0};
    case GDT_Int16: return {-32768.0, 32767.0};
    case GDT_UInt32: return {0.0, 4294967295.0};
    case GDT_Int32: return {-2147483648.0, 2147483647.0};
    default: return {0.0, 1.0};
    }
}

std::pair<double, double> initialBandRange(GDALRasterBand *band)
{
    if (!band)
        return {0.0, 1.0};
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
    double standardDeviation = 0.0;
    if (band->GetStatistics(TRUE, FALSE, &minimum, &maximum, &mean,
                            &standardDeviation) == CE_None
        && std::isfinite(minimum) && std::isfinite(maximum)
        && minimum < maximum) {
        return {minimum, maximum};
    }
    int hasMinimum = FALSE;
    int hasMaximum = FALSE;
    minimum = band->GetMinimum(&hasMinimum);
    maximum = band->GetMaximum(&hasMaximum);
    if (hasMinimum && hasMaximum && std::isfinite(minimum)
        && std::isfinite(maximum) && minimum < maximum) {
        return {minimum, maximum};
    }
    return dataTypeRange(band->GetRasterDataType());
}

struct RasterStatistics
{
    QString layerId;
    QVector<double> minimums;
    QVector<double> maximums;
};

QString noDataText(double value)
{
    return std::isnan(value)
        ? QStringLiteral("nan")
        : QString::number(value, 'g', 15);
}

OgrTransformPtr toWgs84(const OGRSpatialReference *source)
{
    if (!source)
        return {};
    auto sourceCopy = std::unique_ptr<OGRSpatialReference>(source->Clone());
    auto target = std::make_unique<OGRSpatialReference>();
    target->SetWellKnownGeogCS("WGS84");
    sourceCopy->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    target->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    return OgrTransformPtr(
        OGRCreateCoordinateTransformation(sourceCopy.get(), target.get()));
}

} // namespace

AppController::AppController(QObject *parent)
    : QObject(parent),
      m_attributeTableModel(&m_layerModel, this)
{
    GDALAllRegister();
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    const QString savedFont =
        settings.value(QStringLiteral("ui/fontFamily"),
                       QApplication::font().family()).toString();
    m_fontFamily = QFontDatabase::families().contains(savedFont)
        ? savedFont : QApplication::font().family();
    m_fontSize = settings.value(QStringLiteral("ui/fontSize"), 13).toInt();
    m_qtStyle = settings.value(QStringLiteral("ui/qtStyle"),
                               platformDefaultStyle()).toString();
    m_language = settings.value(QStringLiteral("ui/language"),
                                systemDefaultLanguage()).toString();
    m_toolBarOpacity =
        std::clamp(settings.value(QStringLiteral("ui/toolBarOpacity"), 0.85)
                       .toDouble(),
                   0.5, 1.0);
    setStatus(tr("准备就绪"));
}

QString AppController::version() const
{
    return QString::fromLatin1(GEOREADER_VERSION);
}

QString AppController::savedOrPlatformStyle()
{
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    return settings.value(QStringLiteral("ui/qtStyle"), platformDefaultStyle()).toString();
}

QString AppController::savedOrSystemLanguage()
{
    QSettings settings(QString::fromLatin1(kOrganization),
                       QString::fromLatin1(kApplication));
    const QString saved = settings.value(QStringLiteral("ui/language"),
                                         systemDefaultLanguage()).toString();
    return saved.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
        ? QStringLiteral("en_US")
        : QStringLiteral("zh_CN");
}

void AppController::openFiles()
{
    QSettings settings(QString::fromLatin1(kOrganization),
                       QString::fromLatin1(kApplication));
    const QString documentsPath =
        QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString savedPath =
        settings.value(QStringLiteral("files/lastOpenDirectory"),
                       documentsPath).toString();
    // 仅复用仍然存在的目录，避免移动磁盘、网络目录离线后文件对话框
    // 停留在无效位置。
    const QString startPath =
        QFileInfo(savedPath).isDir() ? savedPath : documentsPath;
    const QString filter =
        tr("空间数据 (*.shp *.geojson *.json *.gpkg *.tif *.tiff);;"
           "矢量数据 (*.shp *.geojson *.json *.gpkg);;"
           "栅格数据 (*.tif *.tiff);;所有文件 (*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        nullptr, tr("打开空间数据"), startPath, filter, nullptr, {});
    if (paths.isEmpty())
        return;

    settings.setValue(
        QStringLiteral("files/lastOpenDirectory"),
        QFileInfo(paths.constFirst()).absolutePath());
    loadFiles(paths);
}

void AppController::loadFiles(const QStringList &paths)
{
    for (const auto &path : paths)
        loadDataset(QFileInfo(path).absoluteFilePath());
}

void AppController::setFontFamily(const QString &family)
{
    if (family.isEmpty() || family == m_fontFamily)
        return;
    m_fontFamily = family;
    QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("ui/fontFamily"), family);
    QFont font = QApplication::font();
    font.setFamily(family);
    QApplication::setFont(font);
    emit fontChanged();
}

void AppController::setFontSize(int size)
{
    size = std::clamp(size, 10, 22);
    if (size == m_fontSize)
        return;
    m_fontSize = size;
    QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("ui/fontSize"), size);
    QFont font = QApplication::font();
    font.setPointSize(size);
    QApplication::setFont(font);
    emit fontChanged();
}

void AppController::setQtStyle(const QString &style)
{
    const QString normalized = style.trimmed();
    if (normalized.isEmpty() || normalized == m_qtStyle)
        return;
    m_qtStyle = normalized;
    QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("ui/qtStyle"), normalized);
    emit qtStyleChanged();
    if (!m_restartRequired) {
        m_restartRequired = true;
        emit restartRequiredChanged();
    }
    setStatus(tr("Qt Quick 样式将在下次启动时应用"));
}

void AppController::setLanguage(const QString &language)
{
    const QString normalized =
        language.startsWith(QStringLiteral("en"), Qt::CaseInsensitive)
        ? QStringLiteral("en_US")
        : QStringLiteral("zh_CN");
    if (normalized == m_language)
        return;

    m_language = normalized;
    QSettings(QString::fromLatin1(kOrganization),
              QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("ui/language"), normalized);
    emit languageChanged();
}

void AppController::setToolBarOpacity(double opacity)
{
    opacity = std::clamp(opacity, 0.5, 1.0);
    if (qFuzzyCompare(m_toolBarOpacity, opacity))
        return;
    m_toolBarOpacity = opacity;
    QSettings(QString::fromLatin1(kOrganization),
              QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("ui/toolBarOpacity"), opacity);
    emit toolBarOpacityChanged();
}

QString AppController::shortcut(const QString &action) const
{
    const QString fallback = kDefaultShortcuts.value(action);
    return QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
        .value(QStringLiteral("shortcuts/") + action, fallback).toString();
}

void AppController::setShortcut(const QString &action, const QString &sequence)
{
    if (!kDefaultShortcuts.contains(action) || sequence.trimmed().isEmpty())
        return;
    QSettings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication))
        .setValue(QStringLiteral("shortcuts/") + action, sequence.trimmed());
    emit shortcutsChanged();
}

void AppController::resetShortcuts()
{
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    settings.beginGroup(QStringLiteral("shortcuts"));
    settings.remove(QString());
    settings.endGroup();
    emit shortcutsChanged();
}

void AppController::loadDataset(const QString &path)
{
    if (!QFileInfo::exists(path)) {
        setStatus(tr("文件不存在：%1").arg(path));
        return;
    }

    GdalDatasetPtr vectorDataset(
        static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                              GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    GdalDatasetPtr rasterDataset(
        static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                              GDAL_OF_RASTER | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);

    const bool hasVector = vectorDataset && vectorDataset->GetLayerCount() > 0;
    const bool hasRaster = rasterDataset && rasterDataset->GetRasterCount() > 0;
    if (!hasVector && !hasRaster) {
        setStatus(tr("无法读取该空间数据：%1").arg(QFileInfo(path).fileName()));
        return;
    }

    if (hasVector)
        addVectorLayers(path);
    if (hasRaster)
        addRasterLayer(path);
}

void AppController::addVectorLayers(const QString &path)
{
    GdalDatasetPtr dataset(
        static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                              GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    if (!dataset)
        return;

    int added = 0;
    for (int i = 0; i < dataset->GetLayerCount(); ++i) {
        OGRLayer *ogrLayer = dataset->GetLayer(i);
        if (!ogrLayer)
            continue;
        OGREnvelope extent;
        if (ogrLayer->GetExtent(&extent, TRUE) != OGRERR_NONE)
            continue;

        LayerSnapshot layer;
        layer.path = path;
        layer.sourceLayer = QString::fromUtf8(ogrLayer->GetName());
        layer.name = dataset->GetLayerCount() > 1
            ? QStringLiteral("%1 · %2").arg(QFileInfo(path).completeBaseName(), layer.sourceLayer)
            : QFileInfo(path).completeBaseName();
        layer.type = QStringLiteral("vector");
        layer.geometryType = vectorGeometryType(ogrLayer);
        layer.srs = srsProjString(ogrLayer->GetSpatialRef());
        layer.crsLabel = srsDisplayName(ogrLayer->GetSpatialRef());
        layer.redBand = layer.greenBand = layer.blueBand = layer.grayBand = 0;
        if (!extentToWgs84(extent, ogrLayer->GetSpatialRef(),
                           layer.minLon, layer.minLat, layer.maxLon, layer.maxLat))
            continue;
        m_layerModel.addLayer(layer);
        emit layerAdded(layer.minLon, layer.minLat, layer.maxLon, layer.maxLat);
        ++added;
    }
    setStatus(tr("已加载 %1 个矢量图层").arg(added));
}

void AppController::addRasterLayer(const QString &path)
{
    GdalDatasetPtr dataset(
        static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                              GDAL_OF_RASTER | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    if (!dataset)
        return;

    double transform[6] {};
    if (dataset->GetGeoTransform(transform) != CE_None) {
        setStatus(tr("栅格缺少有效的地理参考：%1").arg(QFileInfo(path).fileName()));
        return;
    }

    const double width = dataset->GetRasterXSize();
    const double height = dataset->GetRasterYSize();
    double xs[] {
        transform[0],
        transform[0] + width * transform[1],
        transform[0] + width * transform[1] + height * transform[2],
        transform[0] + height * transform[2]
    };
    double ys[] {
        transform[3],
        transform[3] + width * transform[4],
        transform[3] + width * transform[4] + height * transform[5],
        transform[3] + height * transform[5]
    };
    OGREnvelope extent;
    extent.MinX = *std::min_element(std::begin(xs), std::end(xs));
    extent.MaxX = *std::max_element(std::begin(xs), std::end(xs));
    extent.MinY = *std::min_element(std::begin(ys), std::end(ys));
    extent.MaxY = *std::max_element(std::begin(ys), std::end(ys));

    LayerSnapshot layer;
    layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    layer.path = path;
    layer.name = QFileInfo(path).completeBaseName();
    layer.type = QStringLiteral("raster");
    layer.bandCount = dataset->GetRasterCount();
    layer.redBand = 1;
    layer.greenBand = std::min(2, layer.bandCount);
    layer.blueBand = std::min(3, layer.bandCount);
    layer.grayBand = 1;
    layer.rasterMode = layer.bandCount >= 3 ? QStringLiteral("rgb") : QStringLiteral("single");
    bool foundNoData = false;
    for (int bandIndex = 1; bandIndex <= layer.bandCount; ++bandIndex) {
        GDALRasterBand *band = dataset->GetRasterBand(bandIndex);
        // Metadata and data-type defaults are effectively instant. A more
        // representative sample is calculated after the layer is visible.
        const auto [minimum, maximum] = initialBandRange(band);
        layer.bandMinimums.push_back(minimum);
        layer.bandMaximums.push_back(maximum);

        if (!foundNoData && band) {
            int hasNoData = FALSE;
            const double noData = band->GetNoDataValue(&hasNoData);
            if (hasNoData) {
                layer.noDataValue = noDataText(noData);
                foundNoData = true;
            }
        }
    }
    layer.noDataEnabled = true;
    layer.srs = srsProjString(dataset->GetSpatialRef());
    layer.crsLabel = srsDisplayName(dataset->GetSpatialRef());
    if (!extentToWgs84(extent, dataset->GetSpatialRef(),
                       layer.minLon, layer.minLat, layer.maxLon, layer.maxLat)) {
        setStatus(tr("无法转换栅格范围：%1").arg(QFileInfo(path).fileName()));
        return;
    }

    m_layerModel.addLayer(layer);
    emit layerAdded(layer.minLon, layer.minLat, layer.maxLon, layer.maxLat);
    setStatus(tr("已加载栅格图层：%1（%2 个波段）")
              .arg(layer.name).arg(layer.bandCount));

    const QString layerId = layer.id;
    QPointer<AppController> self(this);
    // Let the first viewport render win the initial I/O bandwidth. Statistics
    // are useful for refinement, but must not delay the first visible frame.
    QTimer::singleShot(1500, this, [self, path, layerId] {
        if (!self)
            return;
        auto *watcher = new QFutureWatcher<RasterStatistics>(self);
        QObject::connect(
            watcher, &QFutureWatcher<RasterStatistics>::finished, self,
            [self, watcher] {
                const RasterStatistics statistics = watcher->result();
                watcher->deleteLater();
                if (self)
                    self->m_layerModel.setBandRanges(
                        statistics.layerId, statistics.minimums,
                        statistics.maximums);
            });
        watcher->setFuture(QtConcurrent::run([path, layerId] {
            RasterStatistics statistics;
            statistics.layerId = layerId;
            GdalDatasetPtr source(
                static_cast<GDALDataset *>(GDALOpenEx(
                    path.toUtf8().constData(),
                    GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr)),
                GDALClose);
            if (!source)
                return statistics;
            statistics.minimums.reserve(source->GetRasterCount());
            statistics.maximums.reserve(source->GetRasterCount());
            for (int bandIndex = 1;
                 bandIndex <= source->GetRasterCount(); ++bandIndex) {
                const auto [minimum, maximum] =
                    approximateBandRange(source->GetRasterBand(bandIndex));
                statistics.minimums.push_back(minimum);
                statistics.maximums.push_back(maximum);
            }
            return statistics;
        }));
    });
}

QVariantList AppController::queryRasters(double longitude, double latitude) const
{
    // Identify runs on the GUI thread. Reusing read-only handles avoids an
    // expensive GDALOpenEx/metadata scan for every mouse-move sample.
    static QHash<QString, std::shared_ptr<GDALDataset>> datasetCache;
    QVariantList result;
    for (const auto &layer : m_layerModel.snapshots()) {
        if (!layer.visible || layer.type != QStringLiteral("raster"))
            continue;
        auto dataset = datasetCache.value(layer.path);
        if (!dataset) {
            dataset = std::shared_ptr<GDALDataset>(
                static_cast<GDALDataset *>(GDALOpenEx(
                    layer.path.toUtf8().constData(),
                    GDAL_OF_RASTER | GDAL_OF_READONLY,
                    nullptr, nullptr, nullptr)),
                [](GDALDataset *handle) {
                    if (handle)
                        GDALClose(handle);
                });
            if (dataset)
                datasetCache.insert(layer.path, dataset);
        }
        if (!dataset)
            continue;

        double x = longitude;
        double y = latitude;
        if (auto transform = wgs84To(dataset->GetSpatialRef()); transform)
            transform->Transform(1, &x, &y);

        double geoTransform[6] {};
        double inverse[6] {};
        if (dataset->GetGeoTransform(geoTransform) != CE_None
            || !GDALInvGeoTransform(geoTransform, inverse))
            continue;
        const int pixel = static_cast<int>(std::floor(inverse[0] + inverse[1] * x + inverse[2] * y));
        const int line = static_cast<int>(std::floor(inverse[3] + inverse[4] * x + inverse[5] * y));
        if (pixel < 0 || line < 0 || pixel >= dataset->GetRasterXSize()
            || line >= dataset->GetRasterYSize())
            continue;

        QStringList values;
        for (int bandIndex = 1; bandIndex <= dataset->GetRasterCount(); ++bandIndex) {
            double value = std::numeric_limits<double>::quiet_NaN();
            if (dataset->GetRasterBand(bandIndex)->RasterIO(
                    GF_Read, pixel, line, 1, 1, &value, 1, 1, GDT_Float64,
                    0, 0, nullptr) == CE_None) {
                values << tr("B%1: %2").arg(bandIndex).arg(value, 0, 'g', 10);
            }
        }
        result.push_back(QVariantMap {
            {QStringLiteral("name"), layer.name},
            {QStringLiteral("pixel"), QStringLiteral("%1, %2").arg(pixel).arg(line)},
            {QStringLiteral("values"), values}
        });
    }
    return result;
}

QVariantMap AppController::queryVector(int row, double longitude, double latitude,
                                       double toleranceDegrees) const
{
    const LayerSnapshot *layer = m_layerModel.layerAt(row);
    if (!layer || layer->type != QStringLiteral("vector"))
        return {};

    GdalDatasetPtr dataset(
        static_cast<GDALDataset *>(GDALOpenEx(layer->path.toUtf8().constData(),
                                              GDAL_OF_VECTOR | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    if (!dataset)
        return {};
    OGRLayer *ogrLayer = dataset->GetLayerByName(layer->sourceLayer.toUtf8().constData());
    if (!ogrLayer)
        ogrLayer = dataset->GetLayer(0);
    if (!ogrLayer)
        return {};

    double x = longitude;
    double y = latitude;
    double xTolerance = longitude + toleranceDegrees;
    double yTolerance = latitude + toleranceDegrees;
    if (auto transform = wgs84To(ogrLayer->GetSpatialRef()); transform) {
        transform->Transform(1, &x, &y);
        transform->Transform(1, &xTolerance, &yTolerance);
    }
    const double tolerance = std::max(std::abs(xTolerance - x),
                                      std::abs(yTolerance - y));
    const double safeTolerance = std::max(tolerance, 1e-12);
    ogrLayer->SetSpatialFilterRect(x - safeTolerance, y - safeTolerance,
                                   x + safeTolerance, y + safeTolerance);
    ogrLayer->ResetReading();
    OGRPoint queryPoint(x, y);
    using FeaturePtr =
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;
    FeaturePtr closestFeature(nullptr, OGRFeature::DestroyFeature);
    double closestDistance = std::numeric_limits<double>::infinity();
    while (true) {
        FeaturePtr candidate(ogrLayer->GetNextFeature(),
                             OGRFeature::DestroyFeature);
        if (!candidate)
            break;
        const OGRGeometry *geometry = candidate->GetGeometryRef();
        if (!geometry)
            continue;
        const double distance = geometry->Distance(&queryPoint);
        if (distance >= 0.0 && distance <= safeTolerance
            && distance < closestDistance) {
            closestDistance = distance;
            closestFeature = std::move(candidate);
        }
    }
    if (!closestFeature)
        return {};

    QVariantList fields;
    const OGRFeatureDefn *definition = closestFeature->GetDefnRef();
    for (int i = 0; i < definition->GetFieldCount(); ++i) {
        fields.push_back(QVariantMap {
            {QStringLiteral("name"),
             QString::fromUtf8(definition->GetFieldDefn(i)->GetNameRef())},
            {QStringLiteral("value"), fieldValue(closestFeature.get(), i)}
        });
    }

    QString geometryWkt;
    if (const OGRGeometry *sourceGeometry =
            closestFeature->GetGeometryRef()) {
        std::unique_ptr<OGRGeometry> geometry(sourceGeometry->clone());
        if (auto transform = toWgs84(ogrLayer->GetSpatialRef()); transform)
            geometry->transform(transform.get());
        char *wkt = nullptr;
        if (geometry->exportToWkt(&wkt) == OGRERR_NONE && wkt)
            geometryWkt = QString::fromUtf8(wkt);
        CPLFree(wkt);
    }

    return {
        {QStringLiteral("layer"), layer->name},
        {QStringLiteral("fid"), closestFeature->GetFID()},
        {QStringLiteral("fields"), fields},
        {QStringLiteral("geometryWkt"), geometryWkt}
    };
}

QVariantMap AppController::layerMetadata(int row) const
{
    const LayerSnapshot *layer = m_layerModel.layerAt(row);
    if (!layer)
        return {};

    QVariantList entries;
    const auto addEntry =
        [&entries](const QString &section, const QString &name,
                   const QString &value) {
            entries.push_back(QVariantMap {
                {QStringLiteral("section"), section},
                {QStringLiteral("name"), name},
                {QStringLiteral("value"),
                 value.isEmpty() ? QStringLiteral("—") : value}
            });
        };

    const QFileInfo fileInfo(layer->path);
    const QString general = tr("常规");
    const QString spatial = tr("空间信息");
    addEntry(general, tr("名称"), layer->name);
    addEntry(general, tr("文件"), fileInfo.absoluteFilePath());
    addEntry(general, tr("文件大小"),
             QLocale().formattedDataSize(fileInfo.size()));
    addEntry(spatial, tr("坐标参考系"),
             layer->crsLabel.isEmpty() ? tr("未知坐标系")
                                       : layer->crsLabel);
    addEntry(spatial, tr("WGS 84 范围"),
             QStringLiteral("%1, %2 — %3, %4")
                 .arg(layer->minLon, 0, 'g', 12)
                 .arg(layer->minLat, 0, 'g', 12)
                 .arg(layer->maxLon, 0, 'g', 12)
                 .arg(layer->maxLat, 0, 'g', 12));

    // 元信息按需读取，普通加载路径只做首帧显示所需的最小扫描。长 WKT、
    // 字段定义、块大小等详情不会拖慢“打开文件”操作。
    if (layer->type == QStringLiteral("vector")) {
        GdalDatasetPtr dataset(
            static_cast<GDALDataset *>(GDALOpenEx(
                layer->path.toUtf8().constData(),
                GDAL_OF_VECTOR | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr)),
            GDALClose);
        if (!dataset)
            return {{QStringLiteral("title"), layer->name},
                    {QStringLiteral("entries"), entries}};

        OGRLayer *source =
            dataset->GetLayerByName(layer->sourceLayer.toUtf8().constData());
        if (!source)
            source = dataset->GetLayer(0);
        const QString vector = tr("矢量数据");
        addEntry(general, tr("驱动"),
                 QString::fromUtf8(dataset->GetDriverName()));
        addEntry(vector, tr("源图层"), layer->sourceLayer);
        addEntry(vector, tr("几何类型"),
                 source ? QString::fromUtf8(
                              OGRGeometryTypeToName(source->GetGeomType()))
                        : layer->geometryType);
        addEntry(vector, tr("要素数量"),
                 source ? QString::number(source->GetFeatureCount(false))
                        : QString());

        if (source) {
            const OGRFeatureDefn *definition = source->GetLayerDefn();
            addEntry(vector, tr("字段数量"),
                     definition
                         ? QString::number(definition->GetFieldCount())
                         : QStringLiteral("0"));
            if (definition) {
                QStringList fieldDescriptions;
                fieldDescriptions.reserve(definition->GetFieldCount());
                for (int field = 0; field < definition->GetFieldCount();
                     ++field) {
                    const OGRFieldDefn *fieldDefinition =
                        definition->GetFieldDefn(field);
                    fieldDescriptions.push_back(
                        QStringLiteral("%1 (%2)")
                            .arg(QString::fromUtf8(
                                     fieldDefinition->GetNameRef()),
                                 QString::fromUtf8(OGRFieldDefn::GetFieldTypeName(
                                     fieldDefinition->GetType()))));
                }
                addEntry(vector, tr("字段"), fieldDescriptions.join(u'\n'));
            }
            const char *encoding = source->GetMetadataItem("ENCODING");
            addEntry(vector, tr("字符编码"),
                     encoding ? QString::fromUtf8(encoding)
                              : QStringLiteral("UTF-8 / driver default"));

            if (const OGRSpatialReference *reference =
                    source->GetSpatialRef()) {
                char *wkt = nullptr;
                if (reference->exportToPrettyWkt(&wkt, false)
                    == OGRERR_NONE && wkt) {
                    addEntry(spatial, tr("投影定义"),
                             QString::fromUtf8(wkt));
                }
                CPLFree(wkt);
            }
        }
    } else {
        GdalDatasetPtr dataset(
            static_cast<GDALDataset *>(GDALOpenEx(
                layer->path.toUtf8().constData(),
                GDAL_OF_RASTER | GDAL_OF_READONLY,
                nullptr, nullptr, nullptr)),
            GDALClose);
        if (!dataset)
            return {{QStringLiteral("title"), layer->name},
                    {QStringLiteral("entries"), entries}};

        const QString raster = tr("栅格数据");
        addEntry(general, tr("驱动"),
                 QString::fromUtf8(dataset->GetDriverName()));
        addEntry(raster, tr("尺寸"),
                 QStringLiteral("%1 × %2")
                     .arg(dataset->GetRasterXSize())
                     .arg(dataset->GetRasterYSize()));
        addEntry(raster, tr("波段数量"),
                 QString::number(dataset->GetRasterCount()));

        double transform[6] {};
        if (dataset->GetGeoTransform(transform) == CE_None) {
            addEntry(raster, tr("像素大小"),
                     QStringLiteral("%1 × %2")
                         .arg(std::abs(transform[1]), 0, 'g', 12)
                         .arg(std::abs(transform[5]), 0, 'g', 12));
            addEntry(raster, tr("仿射变换"),
                     QStringLiteral("[%1, %2, %3, %4, %5, %6]")
                         .arg(transform[0], 0, 'g', 12)
                         .arg(transform[1], 0, 'g', 12)
                         .arg(transform[2], 0, 'g', 12)
                         .arg(transform[3], 0, 'g', 12)
                         .arg(transform[4], 0, 'g', 12)
                         .arg(transform[5], 0, 'g', 12));
        }

        for (int bandIndex = 1;
             bandIndex <= dataset->GetRasterCount(); ++bandIndex) {
            GDALRasterBand *band = dataset->GetRasterBand(bandIndex);
            if (!band)
                continue;
            int blockWidth = 0;
            int blockHeight = 0;
            band->GetBlockSize(&blockWidth, &blockHeight);
            int hasNoData = FALSE;
            const double noData = band->GetNoDataValue(&hasNoData);
            QString description =
                tr("类型: %1\n颜色解释: %2\n块大小: %3 × %4")
                    .arg(QString::fromLatin1(
                             GDALGetDataTypeName(band->GetRasterDataType())),
                         QString::fromLatin1(GDALGetColorInterpretationName(
                             band->GetColorInterpretation())))
                    .arg(blockWidth)
                    .arg(blockHeight);
            if (hasNoData)
                description += tr("\nNoData: %1").arg(noDataText(noData));
            addEntry(raster, tr("波段 %1").arg(bandIndex), description);
        }

        if (const OGRSpatialReference *reference =
                dataset->GetSpatialRef()) {
            char *wkt = nullptr;
            if (reference->exportToPrettyWkt(&wkt, false)
                == OGRERR_NONE && wkt) {
                addEntry(spatial, tr("投影定义"), QString::fromUtf8(wkt));
            }
            CPLFree(wkt);
        }
    }

    return {
        {QStringLiteral("title"), layer->name},
        {QStringLiteral("type"), layer->type},
        {QStringLiteral("entries"), entries}
    };
}

void AppController::setStatus(const QString &message)
{
    if (message == m_statusMessage)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}
