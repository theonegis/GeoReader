#include "MultidimensionalDataset.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStringList>
#include <QVariantList>

#include <cpl_error.h>
#include <cpl_string.h>
#include <cpl_vsi.h>
#include <gdal.h>
#include <gdal_priv.h>
#include <gdal_utils.h>
#include <ogr_spatialref.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {

using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

template <typename Handle, void (*Release)(Handle)>
class ScopedGdalHandle final {
public:
  explicit ScopedGdalHandle(Handle handle = nullptr) noexcept
      : m_handle(handle) {}
  ScopedGdalHandle(const ScopedGdalHandle &) = delete;
  ScopedGdalHandle &operator=(const ScopedGdalHandle &) = delete;
  ScopedGdalHandle(ScopedGdalHandle &&other) noexcept
      : m_handle(std::exchange(other.m_handle, nullptr)) {}
  ScopedGdalHandle &operator=(ScopedGdalHandle &&other) noexcept {
    if (this != &other) {
      reset();
      m_handle = std::exchange(other.m_handle, nullptr);
    }
    return *this;
  }
  ~ScopedGdalHandle() { reset(); }

  [[nodiscard]] Handle get() const noexcept { return m_handle; }
  explicit operator bool() const noexcept { return m_handle != nullptr; }

private:
  void reset() noexcept {
    if (m_handle)
      Release(m_handle);
    m_handle = nullptr;
  }

  Handle m_handle = nullptr;
};

using GroupHandle = ScopedGdalHandle<GDALGroupH, GDALGroupRelease>;
using ArrayHandle = ScopedGdalHandle<GDALMDArrayH, GDALMDArrayRelease>;
using DataTypeHandle =
    ScopedGdalHandle<GDALExtendedDataTypeH, GDALExtendedDataTypeRelease>;

QString text(const char *value) {
  return value ? QString::fromUtf8(value) : QString();
}

QString spatialReferenceLabel(OGRSpatialReferenceH handle) {
  if (!handle)
    return {};
  auto *source = OGRSpatialReference::FromHandle(handle);
  if (!source)
    return {};
  auto copy = std::unique_ptr<OGRSpatialReference>(source->Clone());
  copy->AutoIdentifyEPSG();
  const char *authority = copy->GetAuthorityName(nullptr);
  const char *code = copy->GetAuthorityCode(nullptr);
  if (authority && code) {
    return QStringLiteral("%1:%2").arg(QString::fromUtf8(authority),
                                       QString::fromUtf8(code));
  }
  return text(copy->GetName());
}

QString spatialReferenceWkt(OGRSpatialReferenceH handle) {
  if (!handle)
    return {};
  auto *source = OGRSpatialReference::FromHandle(handle);
  if (!source)
    return {};
  char *wkt = nullptr;
  const auto result = source->exportToWkt(&wkt);
  const QString value = result == OGRERR_NONE ? text(wkt) : QString();
  CPLFree(wkt);
  return value;
}

bool isXDimension(const MultidimensionalDimension &dimension) {
  const QString token = (dimension.name + QLatin1Char(' ') + dimension.type +
                         QLatin1Char(' ') + dimension.direction)
                            .toLower();
  static const QRegularExpression expression(
      QStringLiteral("(^|[^a-z])(x|lon|longitude|easting)([^a-z]|$)"));
  return dimension.direction.compare(QStringLiteral("east"),
                                     Qt::CaseInsensitive) == 0 ||
         expression.match(token).hasMatch();
}

bool isYDimension(const MultidimensionalDimension &dimension) {
  const QString token = (dimension.name + QLatin1Char(' ') + dimension.type +
                         QLatin1Char(' ') + dimension.direction)
                            .toLower();
  static const QRegularExpression expression(
      QStringLiteral("(^|[^a-z])(y|lat|latitude|northing)([^a-z]|$)"));
  return dimension.direction.compare(QStringLiteral("north"),
                                     Qt::CaseInsensitive) == 0 ||
         expression.match(token).hasMatch();
}

std::pair<int, int>
defaultXYDimensions(const QVector<MultidimensionalDimension> &dimensions) {
  int x = -1;
  int y = -1;
  for (int index = 0; index < dimensions.size(); ++index) {
    if (x < 0 && isXDimension(dimensions.at(index)))
      x = index;
    if (y < 0 && isYDimension(dimensions.at(index)))
      y = index;
  }
  // GDAL 与 CF 数据通常把 Y、X 放在最后两维。启发式只负责预选，
  // 不把名字或数值范围当作 CRS 证据。
  if (dimensions.size() >= 2) {
    if (x < 0)
      x = dimensions.size() - 1;
    if (y < 0)
      y = dimensions.size() - 2;
  }
  if (x == y)
    y = dimensions.size() >= 2 ? dimensions.size() - 2 : -1;
  return {x, y};
}

QHash<QString, QString> subdatasetUris(const QString &path) {
  QHash<QString, QString> result;
  DatasetPtr dataset(
      static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                            GDAL_OF_RASTER | GDAL_OF_READONLY,
                                            nullptr, nullptr, nullptr)),
      GDALClose);
  if (!dataset)
    return result;

  CSLConstList metadata = dataset->GetMetadata("SUBDATASETS");
  if (!metadata || CSLCount(metadata) == 0) {
    if (dataset->GetRasterCount() > 0)
      result.insert(QStringLiteral("*"), path);
    return result;
  }

  QHash<QString, QString> names;
  QHash<QString, QString> descriptions;
  for (int index = 0; metadata[index]; ++index) {
    const QString entry = QString::fromUtf8(metadata[index]);
    const qsizetype equals = entry.indexOf(QLatin1Char('='));
    if (equals <= 0)
      continue;
    const QString key = entry.left(equals);
    const QString value = entry.mid(equals + 1);
    const QRegularExpressionMatch match =
        QRegularExpression(QStringLiteral("SUBDATASET_(\\d+)_(NAME|DESC)"))
            .match(key);
    if (!match.hasMatch())
      continue;
    if (match.captured(2) == QStringLiteral("NAME"))
      names.insert(match.captured(1), value);
    else
      descriptions.insert(match.captured(1), value);
  }
  for (auto iterator = names.cbegin(); iterator != names.cend(); ++iterator) {
    result.insert(iterator.value(), iterator.value());
    result.insert(descriptions.value(iterator.key()), iterator.value());
  }
  return result;
}

QString matchSourceUri(const MultidimensionalArray &array,
                       const QHash<QString, QString> &uris) {
  if (uris.contains(QStringLiteral("*")))
    return uris.value(QStringLiteral("*"));

  const QString fullName = array.fullName;
  const QString name = array.name;
  for (auto iterator = uris.cbegin(); iterator != uris.cend(); ++iterator) {
    const QString haystack = iterator.key();
    if ((!fullName.isEmpty() &&
         haystack.contains(fullName, Qt::CaseInsensitive)) ||
        (!name.isEmpty() && haystack.contains(name, Qt::CaseInsensitive))) {
      return iterator.value();
    }
  }
  return {};
}

QString lastGdalError() {
  const QString message = text(CPLGetLastErrorMsg()).trimmed();
  return message.isEmpty() ? QStringLiteral("GDAL did not provide details")
                           : message;
}

bool hasDriver(const char *name) {
  return GetGDALDriverManager()->GetDriverByName(name) != nullptr;
}

QString viewExpression(const MultidimensionalImportSpec &spec,
                       size_t dimensionCount, QString &error) {
  if (spec.xDimension < 0 || spec.yDimension < 0 ||
      spec.xDimension == spec.yDimension ||
      spec.xDimension >= static_cast<int>(dimensionCount) ||
      spec.yDimension >= static_cast<int>(dimensionCount)) {
    error = QStringLiteral("X/Y 维选择无效");
    return {};
  }

  QStringList selectors;
  selectors.reserve(static_cast<qsizetype>(dimensionCount));
  for (size_t index = 0; index < dimensionCount; ++index) {
    if (static_cast<int>(index) == spec.xDimension ||
        static_cast<int>(index) == spec.yDimension) {
      selectors.push_back(QStringLiteral(":"));
      continue;
    }
    const quint64 slice =
        index < static_cast<size_t>(spec.sliceIndices.size())
            ? spec.sliceIndices.at(static_cast<qsizetype>(index))
            : 0;
    selectors.push_back(QString::number(slice));
  }
  return QStringLiteral("[%1]").arg(selectors.join(QLatin1Char(',')));
}

int retainedDimensionIndex(int originalIndex, int xDimension, int yDimension) {
  int retained = 0;
  const int last = std::max(xDimension, yDimension);
  for (int index = 0; index <= last; ++index) {
    if (index != xDimension && index != yDimension)
      continue;
    if (index == originalIndex)
      return retained;
    ++retained;
  }
  return -1;
}

QString cacheViewPath(const MultidimensionalImportSpec &spec) {
  QFileInfo source(spec.path);
  QByteArray identity = source.absoluteFilePath().toUtf8();
  identity += '|';
  identity += QByteArray::number(source.lastModified().toMSecsSinceEpoch());
  identity += '|';
  identity += spec.arrayFullName.toUtf8();
  identity += '|';
  identity += spec.sourceUri.toUtf8();
  identity += '|';
  identity += QByteArray::number(spec.xDimension);
  identity += '|';
  identity += QByteArray::number(spec.yDimension);
  identity += '|';
  identity += spec.coordinateMode.toUtf8();
  identity += '|';
  identity += spec.crs.toUtf8();
  for (const quint64 index : spec.sliceIndices) {
    identity += '|';
    identity += QByteArray::number(index);
  }
  const QString digest = QString::fromLatin1(
      QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
  QString directory =
      QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
      QStringLiteral("/multidimensional");
  if (!QDir().mkpath(directory)) {
    directory = QDir(QDir::tempPath())
                    .filePath(QStringLiteral("GeoReader/multidimensional"));
    QDir().mkpath(directory);
  }
  return QDir(directory).filePath(digest + QStringLiteral(".vrt"));
}

MultidimensionalScanResult
scanClassicSubdatasets(const QString &path,
                       const QString &multidimensionalError) {
  MultidimensionalScanResult result;
  result.path = path;
  const QHash<QString, QString> candidates = subdatasetUris(path);
  QSet<QString> visited;
  for (const QString &uri : candidates) {
    if (uri.isEmpty() || visited.contains(uri))
      continue;
    visited.insert(uri);
    DatasetPtr dataset(
        static_cast<GDALDataset *>(GDALOpenEx(uri.toUtf8().constData(),
                                              GDAL_OF_RASTER | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
    if (!dataset || dataset->GetRasterCount() < 1 ||
        dataset->GetRasterXSize() < 1 || dataset->GetRasterYSize() < 1) {
      continue;
    }
    if (result.driver.isEmpty() && dataset->GetDriver()) {
      result.driver = QString::fromUtf8(dataset->GetDriver()->GetDescription());
    }

    QString variableName = uri;
    const qsizetype colon = variableName.lastIndexOf(QLatin1Char(':'));
    if (colon >= 0)
      variableName = variableName.mid(colon + 1);
    variableName.remove(QLatin1Char('"'));
    while (variableName.startsWith(QLatin1Char('/')))
      variableName.remove(0, 1);
    if (variableName.isEmpty())
      variableName = QFileInfo(path).completeBaseName();

    MultidimensionalArray array;
    array.name = variableName.section(QLatin1Char('/'), -1);
    array.fullName = variableName;
    array.sourceUri = uri;
    if (GDALRasterBand *band = dataset->GetRasterBand(1)) {
      array.dataType =
          QString::fromLatin1(GDALGetDataTypeName(band->GetRasterDataType()));
      array.unit = text(band->GetUnitType());
      int hasNoData = FALSE;
      const double noData = band->GetNoDataValue(&hasNoData);
      array.hasNoData = hasNoData != FALSE;
      if (array.hasNoData) {
        array.noDataValue = std::isnan(noData)
                                ? QStringLiteral("nan")
                                : QString::number(noData, 'g', 15);
      }
    }
    if (dataset->GetRasterCount() > 1) {
      array.dimensions.push_back(
          {QStringLiteral("band"),
           QStringLiteral("band"),
           QStringLiteral("other"),
           {},
           {},
           static_cast<quint64>(dataset->GetRasterCount())});
    }
    array.dimensions.push_back(
        {QStringLiteral("y"),
         QStringLiteral("y"),
         QStringLiteral("horizontal_y"),
         QStringLiteral("north"),
         {},
         static_cast<quint64>(dataset->GetRasterYSize())});
    array.dimensions.push_back(
        {QStringLiteral("x"),
         QStringLiteral("x"),
         QStringLiteral("horizontal_x"),
         QStringLiteral("east"),
         {},
         static_cast<quint64>(dataset->GetRasterXSize())});
    array.defaultXDimension = array.dimensions.size() - 1;
    array.defaultYDimension = array.dimensions.size() - 2;
    array.hasSpatialReference = dataset->GetSpatialRef() != nullptr;
    array.crs = spatialReferenceWkt(OGRSpatialReference::ToHandle(
        const_cast<OGRSpatialReference *>(dataset->GetSpatialRef())));
    array.crsLabel = spatialReferenceLabel(OGRSpatialReference::ToHandle(
        const_cast<OGRSpatialReference *>(dataset->GetSpatialRef())));
    result.arrays.push_back(std::move(array));
  }
  if (result.arrays.isEmpty())
    result.error = multidimensionalError;
  return result;
}

} // namespace

QVariantMap MultidimensionalArray::toVariantMap() const {
  QVariantList dimensionValues;
  dimensionValues.reserve(dimensions.size());
  for (int index = 0; index < dimensions.size(); ++index) {
    const auto &dimension = dimensions.at(index);
    dimensionValues.push_back(QVariantMap{
        {QStringLiteral("index"), index},
        {QStringLiteral("name"), dimension.name},
        {QStringLiteral("fullName"), dimension.fullName},
        {QStringLiteral("type"), dimension.type},
        {QStringLiteral("direction"), dimension.direction},
        {QStringLiteral("indexingVariable"), dimension.indexingVariable},
        {QStringLiteral("size"), QVariant::fromValue(dimension.size)}});
  }
  return {{QStringLiteral("name"), name},
          {QStringLiteral("fullName"), fullName},
          {QStringLiteral("dataType"), dataType},
          {QStringLiteral("unit"), unit},
          {QStringLiteral("crs"), crs},
          {QStringLiteral("crsLabel"), crsLabel},
          {QStringLiteral("noDataValue"), noDataValue},
          {QStringLiteral("sourceUri"), sourceUri},
          {QStringLiteral("dimensions"), dimensionValues},
          {QStringLiteral("defaultXDimension"), defaultXDimension},
          {QStringLiteral("defaultYDimension"), defaultYDimension},
          {QStringLiteral("hasNoData"), hasNoData},
          {QStringLiteral("hasSpatialReference"), hasSpatialReference}};
}

QVariantMap MultidimensionalScanResult::toVariantMap() const {
  QVariantList arrayValues;
  arrayValues.reserve(arrays.size());
  for (const auto &array : arrays)
    arrayValues.push_back(array.toVariantMap());
  return {{QStringLiteral("path"), path},
          {QStringLiteral("fileName"), QFileInfo(path).fileName()},
          {QStringLiteral("datasetName"), QFileInfo(path).completeBaseName()},
          {QStringLiteral("driver"), driver},
          {QStringLiteral("error"), error},
          {QStringLiteral("arrays"), arrayValues}};
}

MultidimensionalScanResult
MultidimensionalDatasetInspector::scan(const QString &path) {
  MultidimensionalScanResult result;
  result.path = path;
  CPLErrorReset();
  DatasetPtr dataset(static_cast<GDALDataset *>(
                         GDALOpenEx(path.toUtf8().constData(),
                                    GDAL_OF_MULTIDIM_RASTER | GDAL_OF_READONLY,
                                    nullptr, nullptr, nullptr)),
                     GDALClose);
  if (!dataset) {
    const QString error =
        QStringLiteral("无法以 GDAL 多维数据集或子数据集打开：%1")
            .arg(lastGdalError());
    return scanClassicSubdatasets(path, error);
  }
  if (dataset->GetDriver())
    result.driver = QString::fromUtf8(dataset->GetDriver()->GetDescription());

  GroupHandle root(GDALDatasetGetRootGroup(dataset.get()));
  if (!root) {
    return scanClassicSubdatasets(
        path,
        QStringLiteral("数据集没有可读取的根 Group：%1").arg(lastGdalError()));
  }

  char **fullNames =
      GDALGroupGetMDArrayFullNamesRecursive(root.get(), nullptr, nullptr);
  if (!fullNames) {
    result.error = QStringLiteral("没有扫描到多维数组");
    return result;
  }

  const QHash<QString, QString> uris = subdatasetUris(path);
  for (int nameIndex = 0; fullNames[nameIndex]; ++nameIndex) {
    ArrayHandle array(GDALGroupOpenMDArrayFromFullname(
        root.get(), fullNames[nameIndex], nullptr));
    if (!array)
      continue;

    DataTypeHandle dataType(GDALMDArrayGetDataType(array.get()));
    if (!dataType ||
        GDALExtendedDataTypeGetClass(dataType.get()) != GEDTC_NUMERIC)
      continue;

    size_t dimensionCount = 0;
    GDALDimensionH *dimensions =
        GDALMDArrayGetDimensions(array.get(), &dimensionCount);
    if (!dimensions || dimensionCount < 2) {
      if (dimensions)
        GDALReleaseDimensions(dimensions, dimensionCount);
      continue;
    }

    MultidimensionalArray descriptor;
    descriptor.name = text(GDALMDArrayGetName(array.get()));
    descriptor.fullName = text(GDALMDArrayGetFullName(array.get()));
    descriptor.dataType = text(GDALGetDataTypeName(
        GDALExtendedDataTypeGetNumericDataType(dataType.get())));
    descriptor.unit = text(GDALMDArrayGetUnit(array.get()));
    descriptor.dimensions.reserve(static_cast<qsizetype>(dimensionCount));
    for (size_t index = 0; index < dimensionCount; ++index) {
      GDALDimensionH dimension = dimensions[index];
      MultidimensionalDimension dimensionDescriptor;
      dimensionDescriptor.name = text(GDALDimensionGetName(dimension));
      dimensionDescriptor.fullName = text(GDALDimensionGetFullName(dimension));
      dimensionDescriptor.type = text(GDALDimensionGetType(dimension));
      dimensionDescriptor.direction =
          text(GDALDimensionGetDirection(dimension));
      dimensionDescriptor.size = GDALDimensionGetSize(dimension);
      ArrayHandle indexingVariable(GDALDimensionGetIndexingVariable(dimension));
      if (indexingVariable) {
        dimensionDescriptor.indexingVariable =
            text(GDALMDArrayGetFullName(indexingVariable.get()));
      }
      descriptor.dimensions.push_back(std::move(dimensionDescriptor));
    }
    GDALReleaseDimensions(dimensions, dimensionCount);

    const auto [x, y] = defaultXYDimensions(descriptor.dimensions);
    descriptor.defaultXDimension = x;
    descriptor.defaultYDimension = y;
    OGRSpatialReferenceH spatialReference =
        GDALMDArrayGetSpatialRef(array.get());
    descriptor.hasSpatialReference = spatialReference != nullptr;
    descriptor.crs = spatialReferenceWkt(spatialReference);
    descriptor.crsLabel = spatialReferenceLabel(spatialReference);
    int hasNoData = FALSE;
    const double noData =
        GDALMDArrayGetNoDataValueAsDouble(array.get(), &hasNoData);
    descriptor.hasNoData = hasNoData != FALSE;
    if (descriptor.hasNoData) {
      descriptor.noDataValue = std::isnan(noData)
                                   ? QStringLiteral("nan")
                                   : QString::number(noData, 'g', 15);
    }
    descriptor.sourceUri = matchSourceUri(descriptor, uris);
    result.arrays.push_back(std::move(descriptor));
  }
  CSLDestroy(fullNames);

  std::sort(result.arrays.begin(), result.arrays.end(),
            [](const auto &left, const auto &right) {
              return left.fullName.localeAwareCompare(right.fullName) < 0;
            });
  if (result.arrays.isEmpty()) {
    return scanClassicSubdatasets(
        path, QStringLiteral("没有找到至少二维的数值变量；坐标变量和字符串变量"
                             "不会作为图层加载"));
  }
  return result;
}

QVariantMap MultidimensionalDatasetInspector::driverCapabilities() {
  const bool hdf4 = hasDriver("HDF4") || hasDriver("HDF4Image");
  return {{QStringLiteral("netcdf"), hasDriver("netCDF")},
          {QStringLiteral("hdf5"), hasDriver("HDF5") || hasDriver("HDF5Image")},
          {QStringLiteral("hdf4"), hdf4},
          {QStringLiteral("hdf4Message"),
           hdf4
               ? QString()
               : QStringLiteral("当前 GDAL 安装包未包含 HDF4/HDF4Image 驱动")}};
}

PreparedMultidimensionalRaster
MultidimensionalDatasetInspector::prepareRasterView(
    const MultidimensionalImportSpec &spec) {
  PreparedMultidimensionalRaster result;
  CPLErrorReset();

  const int classicDimensionCount = spec.dimensionSizes.size();
  const bool classicAxisOrder = !spec.sourceUri.isEmpty() &&
                                classicDimensionCount >= 2 &&
                                spec.xDimension == classicDimensionCount - 1 &&
                                spec.yDimension == classicDimensionCount - 2;
  if (classicAxisOrder) {
    DatasetPtr classicSource(static_cast<GDALDataset *>(
                                 GDALOpenEx(spec.sourceUri.toUtf8().constData(),
                                            GDAL_OF_RASTER | GDAL_OF_READONLY,
                                            nullptr, nullptr, nullptr)),
                             GDALClose);
    if (classicSource && classicSource->GetRasterCount() > 0) {
      quint64 flattenedBand = 0;
      quint64 multiplier = 1;
      for (int index = classicDimensionCount - 3; index >= 0; --index) {
        const quint64 slice =
            index < spec.sliceIndices.size() ? spec.sliceIndices.at(index) : 0;
        flattenedBand += slice * multiplier;
        multiplier *= std::max<quint64>(1, spec.dimensionSizes.at(index));
      }
      const int band = static_cast<int>(std::min<quint64>(
          flattenedBand + 1,
          static_cast<quint64>(classicSource->GetRasterCount())));
      const QString cachePath = cacheViewPath(spec);
      VSIUnlink(cachePath.toUtf8().constData());
      QStringList optionStrings{QStringLiteral("-of"), QStringLiteral("VRT"),
                                QStringLiteral("-b"), QString::number(band)};
      if (spec.coordinateMode != QStringLiteral("pixel") &&
          !spec.crs.trimmed().isEmpty()) {
        optionStrings << QStringLiteral("-a_srs") << spec.crs.trimmed();
      }
      QVector<QByteArray> encoded;
      encoded.reserve(optionStrings.size());
      for (const QString &option : optionStrings)
        encoded.push_back(option.toUtf8());
      QVector<char *> arguments;
      arguments.reserve(encoded.size() + 1);
      for (QByteArray &option : encoded)
        arguments.push_back(option.data());
      arguments.push_back(nullptr);
      std::unique_ptr<GDALTranslateOptions, decltype(&GDALTranslateOptionsFree)>
          options(GDALTranslateOptionsNew(arguments.data(), nullptr),
                  GDALTranslateOptionsFree);
      int usageError = FALSE;
      DatasetPtr translated(
          options ? static_cast<GDALDataset *>(GDALTranslate(
                        cachePath.toUtf8().constData(), classicSource.get(),
                        options.get(), &usageError))
                  : nullptr,
          GDALClose);
      if (translated && !usageError) {
        result.width = translated->GetRasterXSize();
        result.height = translated->GetRasterYSize();
        double transform[6]{};
        result.hasGeoreference =
            spec.coordinateMode != QStringLiteral("pixel") &&
            translated->GetGeoTransform(transform) == CE_None &&
            translated->GetSpatialRef() != nullptr;
        translated.reset();
        classicSource.reset();
        DatasetPtr verification(
            static_cast<GDALDataset *>(GDALOpenEx(
                cachePath.toUtf8().constData(),
                GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr)),
            GDALClose);
        if (verification && verification->GetRasterCount() == 1) {
          result.sourceUri = cachePath;
          return result;
        }
      }
      CPLErrorReset();
    }
  }

  DatasetPtr multidimensional(static_cast<GDALDataset *>(GDALOpenEx(
                                  spec.path.toUtf8().constData(),
                                  GDAL_OF_MULTIDIM_RASTER | GDAL_OF_READONLY,
                                  nullptr, nullptr, nullptr)),
                              GDALClose);
  if (!multidimensional) {
    result.error =
        QStringLiteral("无法重新打开多维数据集：%1").arg(lastGdalError());
    return result;
  }
  GroupHandle root(GDALDatasetGetRootGroup(multidimensional.get()));
  if (!root) {
    result.error = QStringLiteral("多维数据集没有根 Group");
    return result;
  }
  ArrayHandle source(GDALGroupOpenMDArrayFromFullname(
      root.get(), spec.arrayFullName.toUtf8().constData(), nullptr));
  if (!source) {
    result.error = QStringLiteral("找不到变量 %1：%2")
                       .arg(spec.arrayFullName, lastGdalError());
    return result;
  }

  const size_t dimensionCount = GDALMDArrayGetDimensionCount(source.get());
  QString expressionError;
  const QString expression =
      viewExpression(spec, dimensionCount, expressionError);
  if (expression.isEmpty()) {
    result.error = expressionError;
    return result;
  }
  ArrayHandle view(
      GDALMDArrayGetView(source.get(), expression.toUtf8().constData()));
  if (!view) {
    result.error = QStringLiteral("无法创建变量切片 %1：%2")
                       .arg(expression, lastGdalError());
    return result;
  }

  const int xViewDimension =
      retainedDimensionIndex(spec.xDimension, spec.xDimension, spec.yDimension);
  const int yViewDimension =
      retainedDimensionIndex(spec.yDimension, spec.xDimension, spec.yDimension);
  DatasetPtr classic(
      static_cast<GDALDataset *>(GDALMDArrayAsClassicDatasetEx(
          view.get(), static_cast<size_t>(xViewDimension),
          static_cast<size_t>(yViewDimension), root.get(), nullptr)),
      GDALClose);
  if (!classic) {
    result.error =
        QStringLiteral("变量切片无法转换为二维栅格：%1").arg(lastGdalError());
    return result;
  }

  result.width = classic->GetRasterXSize();
  result.height = classic->GetRasterYSize();
  double transform[6]{};
  result.hasGeoreference = classic->GetGeoTransform(transform) == CE_None &&
                           classic->GetSpatialRef() != nullptr;

  const bool forcePixel = spec.coordinateMode == QStringLiteral("pixel");
  if (!forcePixel && !spec.crs.trimmed().isEmpty()) {
    OGRSpatialReference spatialReference;
    spatialReference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    if (spatialReference.SetFromUserInput(spec.crs.toUtf8().constData()) !=
        OGRERR_NONE) {
      result.error = QStringLiteral("无法识别 CRS：%1").arg(spec.crs);
      return result;
    }
    if (classic->SetSpatialRef(&spatialReference) != CE_None) {
      result.error =
          QStringLiteral("无法把 CRS 应用于变量视图：%1").arg(lastGdalError());
      return result;
    }
    result.hasGeoreference = classic->GetGeoTransform(transform) == CE_None;
  }
  if (forcePixel)
    result.hasGeoreference = false;

  const QString cachePath = cacheViewPath(spec);
  GDALDriver *vrtDriver = GetGDALDriverManager()->GetDriverByName("VRT");
  if (!vrtDriver) {
    result.error = QStringLiteral("当前 GDAL 缺少 VRT 驱动");
    return result;
  }
  DatasetPtr cached(vrtDriver->CreateCopy(cachePath.toUtf8().constData(),
                                          classic.get(), FALSE, nullptr,
                                          nullptr, nullptr),
                    GDALClose);
  if (!cached) {
    result.error =
        QStringLiteral("无法建立多维变量的会话缓存：%1").arg(lastGdalError());
    return result;
  }
  cached.reset();

  // 部分驱动能把 MDArray 的经典视图序列化成真正按需读取的 VRT；
  // 另一些驱动返回的是没有 SourceFilename 的内存视图。先验证 VRT，
  // 不可重开时再退化为后台生成的平铺 GeoTIFF 缓存，保证正确性。
  DatasetPtr verification(
      static_cast<GDALDataset *>(GDALOpenEx(cachePath.toUtf8().constData(),
                                            GDAL_OF_RASTER | GDAL_OF_READONLY,
                                            nullptr, nullptr, nullptr)),
      GDALClose);
  QString preparedPath = cachePath;
  if (!verification || verification->GetRasterCount() < 1) {
    verification.reset();
    VSIUnlink(cachePath.toUtf8().constData());
    GDALDriver *geotiffDriver =
        GetGDALDriverManager()->GetDriverByName("GTiff");
    if (!geotiffDriver) {
      result.error =
          QStringLiteral("VRT 不能引用该变量，且当前 GDAL 缺少 GTiff 缓存驱动");
      return result;
    }
    preparedPath =
        cachePath.left(cachePath.size() - 4) + QStringLiteral(".tif");
    char **options = nullptr;
    options = CSLSetNameValue(options, "TILED", "YES");
    options = CSLSetNameValue(options, "COMPRESS", "DEFLATE");
    options = CSLSetNameValue(options, "PREDICTOR", "2");
    DatasetPtr materialized(
        geotiffDriver->CreateCopy(preparedPath.toUtf8().constData(),
                                  classic.get(), FALSE, options, nullptr,
                                  nullptr),
        GDALClose);
    CSLDestroy(options);
    if (!materialized) {
      result.error =
          QStringLiteral("无法建立多维变量缓存：%1").arg(lastGdalError());
      return result;
    }
    materialized.reset();
  }

  classic.reset();
  view = ArrayHandle();
  source = ArrayHandle();
  root = GroupHandle();
  multidimensional.reset();
  verification.reset();

  // 只有在关闭全部源句柄后重新打开成功，才把缓存交给渲染线程。
  verification = DatasetPtr(
      static_cast<GDALDataset *>(GDALOpenEx(preparedPath.toUtf8().constData(),
                                            GDAL_OF_RASTER | GDAL_OF_READONLY,
                                            nullptr, nullptr, nullptr)),
      GDALClose);
  if (!verification || verification->GetRasterCount() < 1) {
    result.error =
        QStringLiteral("多维变量缓存无法独立重新打开：%1").arg(lastGdalError());
    return result;
  }
  result.sourceUri = preparedPath;
  return result;
}
