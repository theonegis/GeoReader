#include "MultidimensionalDataset.h"
#include "RasterRenderer.h"

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryDir>

#include <gdal_priv.h>
#include <ogr_spatialref.h>

#include <iostream>
#include <memory>

namespace {

using DatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;

bool expect(bool condition, const char *message) {
  if (!condition)
    std::cerr << message << '\n';
  return condition;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication application(argc, argv);
  GDALAllRegister();

  GDALDriver *netcdf = GetGDALDriverManager()->GetDriverByName("netCDF");
  if (!netcdf) {
    std::cout << "SKIP: netCDF driver is unavailable\n";
    return 0;
  }
  QTemporaryDir directory;
  if (!expect(directory.isValid(), "Could not create a temporary directory"))
    return 1;

  GDALDriver *memory = GetGDALDriverManager()->GetDriverByName("MEM");
  DatasetPtr source(memory ? memory->Create("", 16, 12, 1, GDT_Float32, nullptr)
                           : nullptr,
                    GDALClose);
  if (!expect(source != nullptr, "Could not create the source raster"))
    return 2;

  QVector<float> values(16 * 12);
  for (int index = 0; index < values.size(); ++index)
    values[index] = static_cast<float>(index);
  if (!expect(source->GetRasterBand(1)->RasterIO(
                  GF_Write, 0, 0, 16, 12, values.data(), 16, 12, GDT_Float32, 0,
                  0, nullptr) == CE_None,
              "Could not populate the source raster")) {
    return 3;
  }
  double transform[]{100.0, 0.1, 0.0, 40.0, 0.0, -0.1};
  source->SetGeoTransform(transform);
  OGRSpatialReference wgs84;
  wgs84.SetWellKnownGeogCS("WGS84");
  source->SetSpatialRef(&wgs84);

  const QString netcdfPath =
      QDir(directory.path()).filePath(QStringLiteral("sample.nc"));
  DatasetPtr stored(netcdf->CreateCopy(netcdfPath.toUtf8().constData(),
                                       source.get(), FALSE, nullptr, nullptr,
                                       nullptr),
                    GDALClose);
  if (!expect(stored != nullptr, "Could not create the NetCDF fixture"))
    return 4;
  stored.reset();
  source.reset();

  const MultidimensionalScanResult scan =
      MultidimensionalDatasetInspector::scan(netcdfPath);
  if (!expect(scan.isValid(), "NetCDF multidimensional scan failed") ||
      !expect(!scan.arrays.isEmpty(), "No numeric array was discovered")) {
    std::cerr << scan.error.toStdString() << '\n';
    return 5;
  }

  const MultidimensionalArray &array = scan.arrays.constFirst();
  MultidimensionalImportSpec spec;
  spec.path = netcdfPath;
  spec.arrayFullName = array.fullName;
  spec.sourceUri = array.sourceUri;
  spec.xDimension = array.defaultXDimension;
  spec.yDimension = array.defaultYDimension;
  spec.coordinateMode = QStringLiteral("auto");
  spec.crs = array.crs;
  spec.sliceIndices.fill(0, array.dimensions.size());
  for (const auto &dimension : array.dimensions)
    spec.dimensionSizes.push_back(dimension.size);
  const PreparedMultidimensionalRaster prepared =
      MultidimensionalDatasetInspector::prepareRasterView(spec);
  if (!expect(prepared.isValid(), "Could not prepare a classic raster view") ||
      !expect(prepared.width == 16 && prepared.height == 12,
              "Prepared raster dimensions are incorrect") ||
      !expect(prepared.hasGeoreference,
              "Prepared raster lost its georeference")) {
    std::cerr << prepared.error.toStdString() << '\n';
    return 6;
  }

  MultidimensionalImportSpec fallbackSpec = spec;
  fallbackSpec.sourceUri.clear();
  const PreparedMultidimensionalRaster fallback =
      MultidimensionalDatasetInspector::prepareRasterView(fallbackSpec);
  if (!expect(fallback.isValid(), "MDArray fallback preparation failed")) {
    std::cerr << fallback.error.toStdString() << '\n';
    return 6;
  }

  DatasetPtr reopened(static_cast<GDALDataset *>(
                          GDALOpenEx(prepared.sourceUri.toUtf8().constData(),
                                     GDAL_OF_RASTER | GDAL_OF_READONLY, nullptr,
                                     nullptr, nullptr)),
                      GDALClose);
  if (!expect(reopened != nullptr,
              "Prepared VRT did not survive source handle closure") ||
      !expect(reopened->GetRasterCount() == 1,
              "Prepared VRT has an unexpected band count")) {
    return 7;
  }

  LayerSnapshot pixelLayer;
  pixelLayer.path = netcdfPath;
  pixelLayer.sourceUri = prepared.sourceUri;
  pixelLayer.type = QStringLiteral("raster");
  pixelLayer.coordinateMode = QStringLiteral("pixel");
  pixelLayer.bandCount = 1;
  pixelLayer.rasterMode = QStringLiteral("single");
  pixelLayer.grayBand = 1;
  pixelLayer.bandMinimums = {0.0};
  pixelLayer.bandMaximums = {191.0};
  const RasterRenderResult rendered = renderRasterLayer(
      pixelLayer, {QStringLiteral("pixel"), 160, 120, 0.0, 0.0, 16.0, 12.0});
  if (!expect(rendered.error.isEmpty(),
              "Pixel-mode raster rendering returned an error") ||
      !expect(!rendered.image.isNull(),
              "Pixel-mode raster rendering returned no image") ||
      !expect(rendered.image.size() == QSize(160, 120),
              "Pixel-mode raster rendering returned the wrong size")) {
    std::cerr << rendered.error.toStdString() << '\n';
    return 8;
  }

  return 0;
}
