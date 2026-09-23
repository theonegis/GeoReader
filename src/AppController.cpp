#include "AppController.h"
#include "ScientificData.h"
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QSaveFile>

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>

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
#if defined(Q_OS_MACOS)
    return QStringLiteral("macOS");
#elif defined(Q_OS_WIN)
    return QStringLiteral("FluentWinUI3");
#else
    return QStringLiteral("Material");
#endif
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
    if (band->GetStatistics(TRUE, FALSE, &minimum, &maximum, &mean,
                            &standardDeviation) != CE_None
        || !std::isfinite(minimum) || !std::isfinite(maximum)) {
        int hasMinimum = FALSE;
        int hasMaximum = FALSE;
        minimum = band->GetMinimum(&hasMinimum);
        maximum = band->GetMaximum(&hasMaximum);
        if (!hasMinimum || !hasMaximum) {
            switch (band->GetRasterDataType()) {
            case GDT_Byte: return {0.0, 255.0};
            case GDT_UInt16: return {0.0, 65535.0};
            case GDT_Int16: return {-32768.0, 32767.0};
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
    : QObject(parent)
{
    GDALAllRegister();
    QSettings settings(QString::fromLatin1(kOrganization), QString::fromLatin1(kApplication));
    m_fontFamily = settings.value(QStringLiteral("ui/fontFamily"),
                                  QApplication::font().family()).toString();
    m_fontSize = settings.value(QStringLiteral("ui/fontSize"), 13).toInt();
    m_qtStyle = settings.value(QStringLiteral("ui/qtStyle"),
                               platformDefaultStyle()).toString();
    m_toolBarOpacity = QSettings(kOrganization,kApplication).value("ui/toolBarOpacity",.85).toDouble();
    connect(&m_layerModel, &LayerModel::countChanged, this, [this] {
        if(!m_inspectedId.isEmpty() && m_layerModel.rowForId(m_inspectedId)<0) {
            ++m_previewGeneration; ++m_seriesGeneration;m_inspectedId.clear();m_scientificView.clear();m_timeSeries.clear();
            emit scientificViewChanged();emit timeSeriesChanged();
        }
    });
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

void AppController::openFiles()
{
    QString startPath = QSettings(kOrganization, kApplication).value("files/lastDirectory", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)).toString();
    if (!QFileInfo(startPath).isDir()) startPath = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString filter =
        tr("空间数据 (*.shp *.geojson *.json *.gpkg *.tif *.tiff *.h5 *.hdf5 *.he5 *.hdf *.h4 *.hdf4 *.hdf-eos *.nc *.nc4 *.cdf);;"
           "矢量数据 (*.shp *.geojson *.json *.gpkg);;"
           "栅格数据 (*.tif *.tiff *.h5 *.hdf5 *.he5 *.hdf *.h4 *.hdf4 *.hdf-eos *.nc *.nc4 *.cdf);;所有文件 (*)");
    const QStringList paths = QFileDialog::getOpenFileNames(
        nullptr, tr("打开空间数据"), startPath, filter, nullptr, {});
    if (paths.isEmpty())
        return;

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

    const auto suffix = QFileInfo(path).suffix().toLower();
    if (QStringList{"h5","hdf5","he5","hdf","h4","hdf4","hdf-eos","nc","nc4","cdf"}.contains(suffix)) {
        m_scientificQueue << path;
        nextScientificFile();
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

void AppController::addRasterLayer(const QString &path, const QString &name)
{
    std::lock_guard lock(ScientificData::ioMutex());
    auto dataset = ScientificData::open(path);
    if (!dataset || dataset->GetRasterCount() == 0) {
        setStatus(tr("无法打开变量切片")); return;
    }
    const bool scientific = !ScientificData::decode(path).isEmpty();
    double transform[6] {};
    const bool geographic = dataset->GetSpatialRef() && dataset->GetGeoTransform(transform) == CE_None;
    if (!geographic && !scientific) {
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
    layer.path = path;
    layer.name = name.isEmpty() ? QFileInfo(path).completeBaseName() : name;
    layer.scientific = scientific;
    layer.geographic = geographic;
    layer.type = QStringLiteral("raster");
    layer.bandCount = dataset->GetRasterCount();
    layer.redBand = 1;
    layer.greenBand = std::min(2, layer.bandCount);
    layer.blueBand = std::min(3, layer.bandCount);
    layer.grayBand = 1;
    layer.rasterMode = !scientific && layer.bandCount >= 3 ? QStringLiteral("rgb") : QStringLiteral("single");
    bool foundNoData = false;
    for (int bandIndex = 1; bandIndex <= layer.bandCount; ++bandIndex) {
        GDALRasterBand *band = dataset->GetRasterBand(bandIndex);
        const auto [minimum, maximum] = approximateBandRange(band);
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
    layer.noDataEnabled = foundNoData;
    if (scientific) layer.stretchMode = QStringLiteral("percent_clip");
    layer.srs = srsProjString(dataset->GetSpatialRef());
    layer.crsLabel = srsDisplayName(dataset->GetSpatialRef());
    if (!extentToWgs84(extent, dataset->GetSpatialRef(),
                       layer.minLon, layer.minLat, layer.maxLon, layer.maxLat)) {
        setStatus(tr("无法转换栅格范围：%1").arg(QFileInfo(path).fileName()));
        return;
    }

    m_layerModel.addLayer(layer);
    if (geographic) emit layerAdded(layer.minLon, layer.minLat, layer.maxLon, layer.maxLat);
    setStatus(tr("已加载栅格图层：%1（%2 个波段）")
              .arg(layer.name).arg(layer.bandCount));
}

QVariantList AppController::queryRasters(double longitude, double latitude) const
{
    std::lock_guard lock(ScientificData::ioMutex());
    QVariantList result;
    for (const auto &layer : m_layerModel.snapshots()) {
        if (!layer.visible || !layer.geographic || layer.type != QStringLiteral("raster"))
            continue;
        auto dataset = ScientificData::open(layer.path);
        if (!dataset)
            continue;

        double x = longitude;
        double y = latitude;
        if (auto transform = wgs84To(dataset->GetSpatialRef()); transform)
            if (!transform->Transform(1, &x, &y)) continue;

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

void AppController::setStatus(const QString &message)
{
    if (message == m_statusMessage)
        return;
    m_statusMessage = message;
    emit statusMessageChanged();
}

QString AppController::savedOrSystemLanguage() {
    return QSettings(kOrganization,kApplication).value("ui/language","zh_CN").toString();
}
void AppController::setLanguage(const QString &language) {
    if(language!=m_language) {m_language=language;QSettings(kOrganization,kApplication).setValue("ui/language",language);emit languageChanged();}
}
void AppController::setToolBarOpacity(double value) {
    m_toolBarOpacity=std::clamp(value,.3,1.0);QSettings(kOrganization,kApplication).setValue("ui/toolBarOpacity",m_toolBarOpacity);emit toolBarOpacityChanged();
}
QVariantMap AppController::layerMetadata(int row) const {
    std::lock_guard lock(ScientificData::ioMutex());
    auto l=m_layerModel.layerAt(row);if(!l)return {};
    QVariantList entries;
    const auto add=[&](const QString &name,const QString &value){entries<<QVariantMap{{"section",tr("数据")},{"name",name},{"value",value}};};
    auto selection=ScientificData::decode(l->path);
    add(tr("文件"),selection.isEmpty()?l->path:selection.value("file").toString());add(tr("坐标系"),l->geographic?l->crsLabel:tr("无地理参考（像素视图）"));
    if(!selection.isEmpty()) add(tr("变量"),selection.value("array").toString());
    if(l->type=="raster") {auto ds=ScientificData::open(l->path);if(ds){add(tr("驱动"),ds->GetDriverName());add(tr("尺寸"),QString("%1 × %2 × %3").arg(ds->GetRasterXSize()).arg(ds->GetRasterYSize()).arg(ds->GetRasterCount()));add(tr("投影"),ds->GetProjectionRef());}}
    return {{"title",l->name},{"entries",entries}};
}
void AppController::scientificJobFinished() { --m_scientificJobs;emit scientificBusyChanged(); }
void AppController::nextScientificFile() {
    if(m_catalogLoading || !m_catalog.isEmpty() || m_scientificQueue.isEmpty())return;
    QString path=m_scientificQueue.takeFirst();m_catalogLoading=true;++m_scientificJobs;emit scientificBusyChanged();
    auto watcher=new QFutureWatcher<QVariantMap>(this);
    connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher]{
        m_catalogLoading=false;m_catalog=watcher->result();watcher->deleteLater();scientificJobFinished();
        emit scientificCatalogChanged();
    });
    watcher->setFuture(QtConcurrent::run([path]{return ScientificData::catalog(path);}));
}
void AppController::cancelScientificSelection() {m_catalog.clear();emit scientificCatalogChanged();nextScientificFile();}
void AppController::selectScientificVariable(int variable,int x,int y,int time,const QVariantList &indices) {
    std::lock_guard lock(ScientificData::ioMutex());
    const auto variables=m_catalog.value("variables").toList();
    if(variable<0 || variable>=variables.size())return;
    auto v=variables[variable].toMap();QVariantMap s{{"file",m_catalog.value("file")},{"array",v.value("name")},{"x",x},{"y",y},{"time",time},{"indices",indices},{"dimensions",v.value("dimensions")},{"attributes",v.value("attributes")},{"unit",v.value("unit")},{"dataType",v.value("dataType")}};
    QString path=ScientificData::encode(s);
    auto ds=ScientificData::open(path);
    if(!ds) {setStatus(tr("维度选择无效：X、Y、时间轴必须不同，索引不能超出范围。"));return;}
    ds.reset();int before=m_layerModel.count();
    addRasterLayer(path,QFileInfo(s.value("file").toString()).fileName()+" · "+v.value("name").toString());
    if(m_layerModel.count()==before)return;
    QSettings(kOrganization,kApplication).setValue("files/lastDirectory",QFileInfo(s.value("file").toString()).absolutePath());
    const auto id=m_layerModel.layerAt(before)->id;inspectScientificLayer(id);cancelScientificSelection();
}
void AppController::inspectScientificLayer(const QString &id) {
    auto l=m_layerModel.layerAt(m_layerModel.rowForId(id));if(!l || !l->scientific)return;
    if(m_inspectedId!=id) {++m_seriesGeneration;m_timeSeries.clear();emit timeSeriesChanged();}
    m_inspectedId=id;const QString path=l->path,name=l->name;const quint64 generation=++m_previewGeneration;
    m_scientificView={{"id",id},{"name",name},{"selection",ScientificData::decode(path)}};emit scientificViewChanged();
    ++m_scientificJobs;emit scientificBusyChanged();auto watcher=new QFutureWatcher<QVariantMap>(this);
    connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher,id,path,generation]{
        auto result=watcher->result();watcher->deleteLater();scientificJobFinished();
        auto layer=m_layerModel.layerAt(m_layerModel.rowForId(id));if(generation!=m_previewGeneration || !layer || layer->path!=path)return;
        int row=m_layerModel.rowForId(id);
        auto display=ScientificData::decode(path).value("display").toMap();
        double lo=result.value("displayMinimum").toDouble(),hi=result.value("displayMaximum").toDouble();
        if(!result.value("displayMinimum").isNull()) {
            if(lo>=hi){double pad=std::max(1.0,std::abs(lo)*.01);lo-=pad;hi+=pad;}
            m_layerModel.setBandRange(row,1,lo,hi);
            m_layerModel.setRasterStyle(row,"single",1,1,1,1,display.value("ramp","Viridis").toString(),display.value("reversed").toBool(),"minmax");
        }
        result.insert("id",id);result.insert("name",layer->name);result.insert("selection",ScientificData::decode(path));m_scientificView=result;emit scientificViewChanged();
    });watcher->setFuture(QtConcurrent::run([path]{return ScientificData::preview(path);}));
}
void AppController::setScientificSlice(const QString &id,int dimension,int index) {
    int row=m_layerModel.rowForId(id);auto l=m_layerModel.layerAt(row);if(!l || !l->scientific)return;
    auto s=ScientificData::decode(l->path);auto indices=s.value("indices").toList();auto dims=s.value("dimensions").toList();
    if(dimension<0 || dimension>=indices.size() || dimension==s.value("x").toInt() || dimension==s.value("y").toInt() || index<0 || index>=dims[dimension].toMap().value("size").toLongLong())return;
    if(indices[dimension].toInt()==index)return;
    indices[dimension]=index;s["indices"]=indices;m_layerModel.setSource(row,ScientificData::encode(s));
    ++m_seriesGeneration;
    if(dimension!=s.value("time").toInt() || m_timeSeries.value("profile").toString()!="time") {m_timeSeries.clear();emit timeSeriesChanged();}
    inspectScientificLayer(id);
}
void AppController::requestTimeSeries(const QString &id,double x,double y,bool geographic,const QString &profile) {
    auto l=m_layerModel.layerAt(m_layerModel.rowForId(id));if(!l || !l->scientific || !l->visible)return;
    QString path=l->path;const quint64 generation=++m_seriesGeneration;++m_scientificJobs;emit scientificBusyChanged();
    m_timeSeries.clear();emit timeSeriesChanged();auto watcher=new QFutureWatcher<QVariantMap>(this);
    connect(watcher,&QFutureWatcher<QVariantMap>::finished,this,[this,watcher,id,path,generation]{
        auto result=watcher->result();watcher->deleteLater();scientificJobFinished();
        auto layer=m_layerModel.layerAt(m_layerModel.rowForId(id));if(generation!=m_seriesGeneration || !layer || !layer->visible || layer->path!=path)return;
        m_timeSeries=result;emit timeSeriesChanged();
    });watcher->setFuture(QtConcurrent::run([path,x,y,geographic,profile]{return ScientificData::series(path,x,y,geographic,profile);}));
}

void AppController::setScientificDisplay(const QString &id,const QString &ramp,bool reversed,bool fixed,double minimum,double maximum) {
    int row=m_layerModel.rowForId(id);auto l=m_layerModel.layerAt(row);if(!l || !l->scientific)return;
    if(fixed && (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum>=maximum)){setStatus(tr("显示最小值必须小于最大值"));return;}
    auto s=ScientificData::decode(l->path);s["display"]=QVariantMap{{"ramp",ramp},{"reversed",reversed},{"fixed",fixed},{"minimum",minimum},{"maximum",maximum}};
    m_layerModel.setSource(row,ScientificData::encode(s));
    m_layerModel.setRasterStyle(row,"single",1,1,1,1,ramp,reversed,fixed?"minmax":"percent_clip");
    if(fixed)m_layerModel.setBandRange(row,1,minimum,maximum);
    inspectScientificLayer(id);
}
void AppController::exportTimeSeries() {
    if(m_timeSeries.value("points").toList().isEmpty())return;
    QString file=QFileDialog::getSaveFileName(nullptr,tr("导出曲线数据"),"pixel-profile.csv",tr("CSV 文件 (*.csv)"));if(file.isEmpty())return;
    if(!file.endsWith(".csv",Qt::CaseInsensitive))file+=".csv";
    QSaveFile output(file);const auto bytes=ScientificData::csv(m_timeSeries);
    if(!output.open(QIODevice::WriteOnly) || output.write(bytes)!=bytes.size() || !output.commit())setStatus(tr("导出失败：%1").arg(output.errorString()));else setStatus(tr("已导出：%1").arg(file));
}
void AppController::exportScientificImage() {
    const auto image=m_scientificView.value("image").toString();if(image.isEmpty())return;
    QString file=QFileDialog::getSaveFileName(nullptr,tr("导出当前预览（最长边 800 像素）"),"variable-preview.png",tr("PNG 图像 (*.png)"));if(file.isEmpty())return;
    if(!file.endsWith(".png",Qt::CaseInsensitive))file+=".png";
    const auto bytes=QByteArray::fromBase64(image.section(',',1).toLatin1());QSaveFile output(file);
    if(!output.open(QIODevice::WriteOnly)||output.write(bytes)!=bytes.size()||!output.commit())setStatus(tr("导出失败：%1").arg(output.errorString()));else setStatus(tr("已导出：%1").arg(file));
}
