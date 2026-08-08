#pragma once

#include <QString>
#include <QVariantMap>
#include <QVector>

struct MultidimensionalDimension {
  QString name;
  QString fullName;
  QString type;
  QString direction;
  QString indexingVariable;
  quint64 size = 0;
};

struct MultidimensionalArray {
  QString name;
  QString fullName;
  QString dataType;
  QString unit;
  QString crs;
  QString crsLabel;
  QString noDataValue;
  QString sourceUri;
  QVector<MultidimensionalDimension> dimensions;
  int defaultXDimension = -1;
  int defaultYDimension = -1;
  bool hasNoData = false;
  bool hasSpatialReference = false;

  [[nodiscard]] QVariantMap toVariantMap() const;
};

struct MultidimensionalScanResult {
  QString path;
  QString driver;
  QString error;
  QVector<MultidimensionalArray> arrays;

  [[nodiscard]] bool isValid() const noexcept {
    return error.isEmpty() && !arrays.isEmpty();
  }

  [[nodiscard]] QVariantMap toVariantMap() const;
};

struct MultidimensionalImportSpec {
  QString path;
  QString arrayFullName;
  QString sourceUri;
  QString coordinateMode = QStringLiteral("auto");
  QString crs;
  QVector<quint64> sliceIndices;
  QVector<quint64> dimensionSizes;
  int xDimension = -1;
  int yDimension = -1;
};

struct PreparedMultidimensionalRaster {
  QString sourceUri;
  QString error;
  bool hasGeoreference = false;
  int width = 0;
  int height = 0;

  [[nodiscard]] bool isValid() const noexcept {
    return error.isEmpty() && !sourceUri.isEmpty();
  }
};

// GDAL 的多维句柄只在扫描函数内部存活。界面与后台渲染只交换
// 值对象，避免把驱动拥有的裸指针带到 Qt Quick 或其他线程。
class MultidimensionalDatasetInspector final {
public:
  [[nodiscard]] static MultidimensionalScanResult scan(const QString &path);
  [[nodiscard]] static QVariantMap driverCapabilities();
  [[nodiscard]] static PreparedMultidimensionalRaster
  prepareRasterView(const MultidimensionalImportSpec &spec);
};
