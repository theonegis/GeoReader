#include "RasterRenderer.h"
#include "ColorRamp.h"
#include "MapCanvas.h"
#include "ScientificData.h"
#include <QColor>
#include <algorithm>
#include <array>
#include <cmath>
#include <gdal_utils.h>
#include <numeric>
#include <vrtdataset.h>

namespace RasterRenderer {
QImage render(const LayerSnapshot &l, const MapViewport &v, QString &error) {
  std::lock_guard lock(ScientificData::ioMutex());
  if (!l.geographic)
    return {};
  auto source = ScientificData::open(l.path);
  if (!source) {
    error = "无法打开栅格：" + l.name;
    return {};
  }
  bool single = l.rasterMode == "single";
  QVector<int> bands = single
                           ? QVector<int>{l.grayBand}
                           : QVector<int>{l.redBand, l.greenBand, l.blueBand};
  auto arguments = [](const QStringList &items) {
    char **args = nullptr;
    for (const auto &s : items)
      args = CSLAddString(args, s.toUtf8().constData());
    return args;
  };
  auto selection =
      new VRTDataset(source->GetRasterXSize(), source->GetRasterYSize());
  ScientificData::Dataset selected(selection, GDALClose);
  double geoTransform[6];
  if (source->GetGeoTransform(geoTransform) == CE_None)
    static_cast<GDALDataset *>(selection)->SetGeoTransform(geoTransform);
  selection->SetSpatialRef(source->GetSpatialRef());
  for (int i = 0; i < bands.size(); ++i) {
    auto srcBand = source->GetRasterBand(bands[i]);
    if (!srcBand) {
      error = "波段无效";
      return {};
    }
    selection->AddBand(srcBand->GetRasterDataType(), nullptr);
    auto band =
        static_cast<VRTSourcedRasterBand *>(selection->GetRasterBand(i + 1));
    band->AddSimpleSource(srcBand);
    int has = 0;
    double nd = srcBand->GetNoDataValue(&has);
    if (has)
      band->SetNoDataValue(nd);
  }
  // VRT sources keep references; ReleaseRef, rather than GDALClose, balances
  // our original ownership.
  source.release()->ReleaseRef();
  QStringList opts;
  opts = {"-of",
          "MEM",
          "-t_srs",
          "EPSG:3857",
          "-te",
          QString::number(v.minMercatorX, 'g', 17),
          QString::number(v.minMercatorY, 'g', 17),
          QString::number(v.maxMercatorX, 'g', 17),
          QString::number(v.maxMercatorY, 'g', 17),
          "-ts",
          QString::number(v.width),
          QString::number(v.height),
          "-ot",
          "Float64",
          "-r",
          "near",
          "-dstalpha"};
  if (l.noDataEnabled)
    opts << "-srcnodata" << l.noDataValue;
  auto args = arguments(opts);
  auto wo = GDALWarpAppOptionsNew(args, nullptr);
  CSLDestroy(args);
  GDALDatasetH handle = selected.get();
  auto warped =
      ScientificData::Dataset(static_cast<GDALDataset *>(GDALWarp(
                                  "", nullptr, 1, &handle, wo, nullptr)),
                              GDALClose);
  GDALWarpAppOptionsFree(wo);
  if (!warped) {
    error = "栅格重投影失败：" + l.name;
    return {};
  }
  size_t n = size_t(v.width) * v.height;
  std::vector<double> alpha(n);
  std::vector<std::vector<double>> data(bands.size(), std::vector<double>(n));
  if (warped->GetRasterBand(warped->GetRasterCount())
          ->RasterIO(GF_Read, 0, 0, v.width, v.height, alpha.data(), v.width,
                     v.height, GDT_Float64, 0, 0, nullptr) != CE_None)
    return {};
  for (int k = 0; k < bands.size(); ++k) {
    if (warped->GetRasterBand(k + 1)->RasterIO(
            GF_Read, 0, 0, v.width, v.height, data[k].data(), v.width, v.height,
            GDT_Float64, 0, 0, nullptr) != CE_None)
      return {};
    if (l.scientific) {
      auto [scale, offset] = ScientificData::scaleOffset(l.path);
      for (auto &x : data[k])
        x = x * scale + offset;
    }
    std::vector<double> sample;
    for (size_t i = 0; i < n; ++i)
      if (alpha[i] > 0 && std::isfinite(data[k][i]))
        sample.push_back(data[k][i]);
    double lo = l.bandMinimums.value(bands[k] - 1, 0),
           hi = l.bandMaximums.value(bands[k] - 1, 1);
    if (!sample.empty() && l.stretchMode != "minmax") {
      std::sort(sample.begin(), sample.end());
      if (l.stretchMode == "standard_deviation") {
        double mean = std::accumulate(sample.begin(), sample.end(), 0.0) /
                      sample.size(),
               var = 0;
        for (double x : sample)
          var += (x - mean) * (x - mean);
        double sd = std::sqrt(var / sample.size());
        lo = mean - 2 * sd;
        hi = mean + 2 * sd;
      } else {
        lo = sample[size_t((sample.size() - 1) * .02)];
        hi = sample[size_t((sample.size() - 1) * .98)];
      }
    }
    for (auto &x : data[k]) {
      if (l.stretchMode == "histogram_equalization" && !sample.empty() &&
          std::isfinite(x))
        x = double(std::lower_bound(sample.begin(), sample.end(), x) -
                   sample.begin()) /
            std::max(size_t(1), sample.size() - 1);
      else
        x = hi > lo ? (x - lo) / (hi - lo) : .5;
      x = std::clamp(x, 0.0, 1.0);
    }
  }
  QImage image(v.width, v.height, QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  for (size_t i = 0; i < n; ++i) {
    if (!(alpha[i] > 0))
      continue;
    bool good = true;
    for (const auto &d : data)
      good = good && std::isfinite(d[i]);
    if (!good)
      continue;
    QColor c;
    if (single)
      c = ColorRamp::color(data[0][i], l.colorRamp, l.colorRampReversed);
    else
      c = QColor::fromRgbF(data[0][i], data[1][i], data[2][i]);
    c.setAlphaF(l.opacity);
    image.setPixelColor(int(i % v.width), int(i / v.width), c);
  }
  return image;
}
} // namespace RasterRenderer
