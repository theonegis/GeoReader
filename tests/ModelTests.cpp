#include "AttributeTableModel.h"
#include "LayerModel.h"
#include "ScientificData.h"
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <cmath>
#include <stdexcept>

static int assertions = 0;
void check(bool ok, const QString &message) {
  ++assertions;
  if (!ok)
    throw std::runtime_error(message.toStdString());
}
void checkSeries(const QString &path, bool packed, int timeAxis = -2) {
  auto cat = ScientificData::catalog(path);
  check(cat.value("error").toString().isEmpty(),
        "catalog: " + cat.value("error").toString());
  auto variables = cat.value("variables").toList();
  check(variables.size() >= 2, "multiple variables discovered");
  QVariantMap v;
  for (auto item : variables)
    if (item.toMap().value("name").toString().endsWith("temperature"))
      v = item.toMap();
  check(!v.isEmpty(), "temperature discovered");
  auto dims = v.value("dimensions").toList();
  QVariantList indices;
  for (int i = 0; i < dims.size(); ++i)
    indices << 0;
  int t = timeAxis == -2 ? v.value("time").toInt() : timeAxis;
  QVariantMap s{{"file", path},      {"array", v.value("name")},
                {"x", v.value("x")}, {"y", v.value("y")},
                {"time", t},         {"indices", indices},
                {"dimensions", dims}};
  auto uri = ScientificData::encode(s);
  check(ScientificData::decode(uri) == s, "selection encoding roundtrip");
  auto ds = ScientificData::open(uri);
  check(bool(ds), "slice opens");
  check(ds->GetRasterCount() == 1, "time not interpreted as RGB");
  check(ds->GetRasterXSize() == 5 && ds->GetRasterYSize() == 4,
        "slice dimensions");
  double pixel = 0;
  check(ds->GetRasterBand(1)->RasterIO(GF_Read, 2, 1, 1, 1, &pixel, 1, 1,
                                       GDT_Float64, 0, 0, nullptr) == CE_None,
        "pixel read");
  check(pixel == 12, "slice pixel t0");
  auto preview = ScientificData::preview(uri);
  check(preview.value("image").toString().startsWith("data:image/png;base64,"),
        "PNG visualization");
  auto series = ScientificData::series(uri, 2, 1, false);
  auto points = series.value("points").toList();
  check(points.size() == 4, "four time steps");
  for (int i = 0; i < 4; ++i)
    check(std::abs(points[i].toMap().value("value").toDouble() -
                   ((100 * i + 12) * (packed ? .5 : 1) + (packed ? 10 : 0))) <
              1e-6,
          "time series numerical value " + QString::number(i));
  if (!path.endsWith(".hdf")) {
    check(points[1].toMap().value("x").toDouble() == 2 &&
              points[3].toMap().value("x").toDouble() == 9,
          "irregular time coordinates preserved");
    check(points[3].toMap().value("label").toString().startsWith("2020-01-10"),
          "CF time label decoded");
  }
  check(!series.value("cells").toList().isEmpty(),
        "neighbourhood table produced");
  check(ScientificData::csv(series).contains(
            "variable,pixel,coordinate,label,value,unit,calendar"),
        "CSV header");
  const auto row =
      ScientificData::series(uri, 2, 1, false, "row").value("points").toList();
  check(row.size() == 5, "row profile length");
  check(std::abs(row[4].toMap().value("value").toDouble() -
                 (14 * (packed ? .5 : 1) + (packed ? 10 : 0))) < 1e-6,
        "row profile value");
  const auto column = ScientificData::series(uri, 2, 1, false, "column")
                          .value("points")
                          .toList();
  check(column.size() == 4, "column profile length");
  check(std::abs(column[3].toMap().value("value").toDouble() -
                 (32 * (packed ? .5 : 1) + (packed ? 10 : 0))) < 1e-6,
        "column profile value");
  if (path.endsWith(".nc")) {
    check(preview.value("geographic").toBool(),
          "CF geographic grid recognized");
    auto geo =
        ScientificData::series(uri, 102, 39, true).value("points").toList();
    check(geo.size() == 4, "map coordinate lookup");
    check(geo[0].toMap().value("value") == points[0].toMap().value("value"),
          "map and pixel lookup agree");
  }
  check(preview.value("histogram").toList().size() == 32,
        "histogram has 32 bins");
  check(preview.value("validCount").toInt() == 20, "slice sample valid count");
  auto constant = s;
  constant["display"] = QVariantMap{
      {"ramp", "Gray"}, {"fixed", true}, {"minimum", 0}, {"maximum", 500}};
  auto manual = ScientificData::preview(ScientificData::encode(constant));
  check(manual.value("displayMaximum").toDouble() == 500,
        "manual scale applied");
  check(manual.value("image") != preview.value("image"),
        "palette and range alter image");
  auto missing =
      ScientificData::series(uri, 1, 1, false).value("points").toList();
  check(missing.size() == 4, "missing series size");
  check(missing[2].toMap().value("value").isNull(), "NoData is a gap");
  check(!ScientificData::series(uri, -.01, 1, false)
             .value("error")
             .toString()
             .isEmpty(),
        "negative pixel rejected");
  check(!ScientificData::series(uri, 5, 1, false)
             .value("error")
             .toString()
             .isEmpty(),
        "outside boundary rejected");
  indices[t] = 3;
  s["indices"] = indices;
  auto last = ScientificData::open(ScientificData::encode(s));
  check(bool(last), "last time slice opens");
  check(last->GetRasterBand(1)->RasterIO(GF_Read, 2, 1, 1, 1, &pixel, 1, 1,
                                         GDT_Float64, 0, 0,
                                         nullptr) == CE_None &&
            pixel == 312,
        "slice change selects correct data");
  if (path.endsWith("levels.nc")) {
    indices[1] = 1;
    s["indices"] = indices;
    auto depth = ScientificData::series(ScientificData::encode(s), 2, 1, false)
                     .value("points")
                     .toList();
    check(depth[0].toMap().value("value").toDouble() == 516,
          "depth dimension fixed independently of time");
  }
  s["x"] = s["y"];
  check(!ScientificData::open(ScientificData::encode(s)),
        "duplicate axes rejected");
  qInfo().noquote() << "PASS" << QFileInfo(path).fileName() << "driver"
                    << cat.value("driver").toString();
}
int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  GDALAllRegister();
  try {
    QString dir = QString::fromUtf8(GEOREADER_TEST_DATA_DIR);
    LayerModel layers;
    LayerSnapshot a;
    a.name = "A";
    a.type = "raster";
    a.bandCount = 1;
    layers.addLayer(a);
    a.name = "B";
    layers.addLayer(a);
    const auto id = layers.layerAt(0)->id;
    check(layers.get(0).value("name") == "A", "model get safe role iteration");
    layers.moveLayer(0, 1);
    check(layers.rowForId(id) == 1, "stable ID after reorder");
    layers.removeLayer(0);
    check(layers.rowForId(id) == 0, "stable ID after remove");
    for (const auto &format : QStringList{"netcdf-classic.nc", "netcdf4.nc",
                                          "scientific.h5", "scientific.hdf"})
      checkSeries(dir + "/" + format, format != "scientific.hdf");
    checkSeries(dir + "/permuted.nc", true);
    checkSeries(dir + "/levels.nc", true);
    // The 2D quality variable supports value inspection and spatial profiles
    // without a time axis.
    auto cat = ScientificData::catalog(dir + "/netcdf4.nc");
    QVariantMap quality;
    for (auto v : cat.value("variables").toList())
      if (v.toMap().value("name").toString().endsWith("quality"))
        quality = v.toMap();
    QVariantMap qs{{"file", dir + "/netcdf4.nc"},
                   {"array", quality.value("name")},
                   {"x", 1},
                   {"y", 0},
                   {"time", -1},
                   {"indices", QVariantList{0, 0}}};
    auto values =
        ScientificData::series(ScientificData::encode(qs), 2, 1, false);
    check(values.value("error").toString().isEmpty() &&
              !values.value("cells").toList().isEmpty(),
          "static 2D value inspection");
    check(ScientificData::catalog(dir + "/missing.nc").contains("error"),
          "missing file error");
    qInfo() << "PASS total assertions:" << assertions;
    return 0;
  } catch (const std::exception &e) {
    qCritical() << "FAIL after" << assertions << "assertions:" << e.what();
    return 1;
  }
}
