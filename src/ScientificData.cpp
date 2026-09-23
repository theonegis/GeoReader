#include "ScientificData.h"
#include "ColorRamp.h"
#include <QBuffer>
#include <QColor>
#include <QDateTime>
#include <QImage>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QTimeZone>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>
#include <vrtdataset.h>

namespace ScientificData {
std::recursive_mutex &ioMutex() {
  static std::recursive_mutex mutex;
  return mutex;
}
namespace {
QString attribute(const std::shared_ptr<GDALMDArray> &a, const char *name) {
  auto v = a->GetAttribute(name);
  return v ? QString::fromUtf8(v->ReadAsString()) : QString();
}
struct Array {
  Dataset file{nullptr, GDALClose};
  std::shared_ptr<GDALMDArray> array;
};
Array arrayFor(const QVariantMap &s) {
  Array a;
  a.file.reset(static_cast<GDALDataset *>(GDALOpenEx(
      s.value("file").toString().toUtf8().constData(),
      GDAL_OF_MULTIDIM_RASTER | GDAL_OF_READONLY, nullptr, nullptr, nullptr)));
  if (a.file && a.file->GetRootGroup())
    a.array = a.file->GetRootGroup()->OpenMDArrayFromFullname(
        s.value("array").toString().toStdString());
  return a;
}
std::pair<double, double> calibration(const std::shared_ptr<GDALMDArray> &a) {
  bool hasScale = false, hasOffset = false;
  double scale = a->GetScale(&hasScale), offset = a->GetOffset(&hasOffset);
  if (!hasScale)
    if (auto attr = a->GetAttribute("scale_factor"))
      scale = attr->ReadAsDouble();
  if (!hasOffset)
    if (auto attr = a->GetAttribute("add_offset"))
      offset = attr->ReadAsDouble();
  return {scale, offset};
}
bool valid(const QVariantMap &s, const std::shared_ptr<GDALMDArray> &a) {
  if (!a || a->GetDataType().GetClass() != GEDTC_NUMERIC)
    return false;
  const int n = int(a->GetDimensions().size());
  const int x = s.value("x", -1).toInt(), y = s.value("y", -1).toInt();
  const int t = s.value("time", -1).toInt();
  if (n < 2 || x < 0 || y < 0 || x >= n || y >= n || x == y || t < -1 ||
      t >= n || t == x || t == y)
    return false;
  auto indices = s.value("indices").toList();
  if (indices.size() != n)
    return false;
  for (int i = 0; i < n; ++i) {
    bool ok = false;
    auto v = indices[i].toLongLong(&ok);
    if (!ok || v < 0 ||
        static_cast<GUInt64>(v) >= a->GetDimensions()[i]->GetSize())
      return false;
  }
  return true;
}
QString timeLabel(double v, const QString &units, const QString &calendar) {
  // Non-Gregorian CF calendars retain their exact numeric coordinate and
  // calendar.
  if (!calendar.isEmpty() && calendar != "standard" &&
      calendar != "gregorian" && calendar != "proleptic_gregorian")
    return QString::number(v, 'g', 12);
  static const QRegularExpression re(
      "^(seconds?|minutes?|hours?|days?) since (.+)$",
      QRegularExpression::CaseInsensitiveOption);
  const auto m = re.match(units);
  if (m.hasMatch()) {
    QString epoch = m.captured(2).trimmed();
    epoch.replace(' ', 'T');
    QDateTime dt = QDateTime::fromString(epoch, Qt::ISODate);
    if (!dt.isValid())
      dt = QDateTime(QDate::fromString(epoch.left(10), Qt::ISODate),
                     QTime(0, 0), QTimeZone::UTC);
    if (dt.isValid()) {
      dt.setTimeZone(QTimeZone::UTC);
      const auto u = m.captured(1).toLower();
      const double scale = u.startsWith("day")      ? 86400
                           : u.startsWith("hour")   ? 3600
                           : u.startsWith("minute") ? 60
                                                    : 1;
      if (std::isfinite(v) && std::abs(v * scale) < 1e12)
        return dt.addMSecs(qRound64(v * scale * 1000)).toString(Qt::ISODate);
    }
  }
  return QString::number(v, 'g', 12);
}
void walk(const std::shared_ptr<GDALGroup> &g, QVariantList &out,
          int depth = 0) {
  if (!g || depth > 32 || out.size() > 10000)
    return;
  for (const auto &name : g->GetMDArrayNames()) {
    auto a = g->OpenMDArray(name);
    if (!a || a->GetDimensionCount() < 2 ||
        a->GetDataType().GetClass() != GEDTC_NUMERIC)
      continue;
    QVariantList dims;
    int x = -1, y = -1, t = -1, i = 0;
    for (const auto &d : a->GetDimensions()) {
      const QString name = QString::fromStdString(d->GetName()),
                    type = QString::fromStdString(d->GetType());
      const QString lower = name.toLower();
      auto coordinate = d->GetIndexingVariable();
      const auto standard =
          coordinate ? attribute(coordinate, "standard_name") : QString();
      const auto axis = coordinate ? attribute(coordinate, "axis") : QString();
      if (type == "HORIZONTAL_X" || axis == "X" || lower == "lon" ||
          lower == "longitude" || lower == "x")
        x = i;
      if (type == "HORIZONTAL_Y" || axis == "Y" || lower == "lat" ||
          lower == "latitude" || lower == "y")
        y = i;
      if (type == "TEMPORAL" || axis == "T" || standard == "time" ||
          lower == "time")
        t = i;
      dims.push_back(
          QVariantMap{{"name", name},
                      {"size", QVariant::fromValue<qulonglong>(d->GetSize())},
                      {"type", type}});
      ++i;
    }
    if (x < 0)
      x = i - 1;
    if (y < 0)
      y = i - 2;
    if (t == x || t == y)
      t = -1;
    QVariantList attributes;
    for (const auto &attr : a->GetAttributes()) {
      if (attr->GetTotalElementsCount() <= 100)
        attributes << QVariantMap{
            {"name", QString::fromStdString(attr->GetName())},
            {"value", QString::fromUtf8(attr->ReadAsString())}};
    }
    out.push_back(
        QVariantMap{{"attributes", attributes},
                    {"dataType", QString::fromUtf8(GDALGetDataTypeName(
                                     a->GetDataType().GetNumericDataType()))},
                    {"name", QString::fromStdString(a->GetFullName())},
                    {"dimensions", dims},
                    {"x", x},
                    {"y", y},
                    {"time", t},
                    {"unit", QString::fromStdString(a->GetUnit())},
                    {"description", attribute(a, "long_name")}});
  }
  for (const auto &name : g->GetGroupNames())
    walk(g->OpenGroup(name), out, depth + 1);
}
} // namespace
QVariantMap catalog(const QString &path) {
  std::lock_guard lock(ioMutex());
  Dataset ds(static_cast<GDALDataset *>(
                 GDALOpenEx(path.toUtf8().constData(),
                            GDAL_OF_MULTIDIM_RASTER | GDAL_OF_READONLY, nullptr,
                            nullptr, nullptr)),
             GDALClose);
  if (!ds || !ds->GetRootGroup())
    return {{"error", QStringLiteral("无法读取多维数据。请检查文件及 GDAL 的 "
                                     "HDF4/HDF5/netCDF 驱动。")}};
  QVariantList arrays;
  walk(ds->GetRootGroup(), arrays);
  return {
      {"file", path},
      {"variables", arrays},
      {"driver", QString::fromUtf8(ds->GetDriverName())},
      {"error", arrays.isEmpty()
                    ? QStringLiteral("文件中没有可显示的二维或多维数值变量。")
                    : QString()}};
}
std::pair<double, double> scaleOffset(const QString &path) {
  std::lock_guard lock(ioMutex());
  auto a = arrayFor(decode(path));
  return a.array ? calibration(a.array) : std::pair<double, double>{1, 0};
}
QString encode(const QVariantMap &s) {
  return "georeader-md:" +
         QString::fromLatin1(QJsonDocument::fromVariant(s)
                                 .toJson(QJsonDocument::Compact)
                                 .toBase64(QByteArray::Base64UrlEncoding));
}
QVariantMap decode(const QString &path) {
  return path.startsWith("georeader-md:")
             ? QJsonDocument::fromJson(
                   QByteArray::fromBase64(path.mid(13).toLatin1(),
                                          QByteArray::Base64UrlEncoding))
                   .toVariant()
                   .toMap()
             : QVariantMap();
}
Dataset open(const QString &path) {
  std::lock_guard lock(ioMutex());
  auto s = decode(path);
  if (s.isEmpty())
    return Dataset(
        static_cast<GDALDataset *>(GDALOpenEx(path.toUtf8().constData(),
                                              GDAL_OF_RASTER | GDAL_OF_READONLY,
                                              nullptr, nullptr, nullptr)),
        GDALClose);
  auto a = arrayFor(s);
  if (!valid(s, a.array))
    return {nullptr, GDALClose};
  QStringList slices;
  const auto indices = s.value("indices").toList();
  int x = s.value("x").toInt(), y = s.value("y").toInt();
  for (int i = 0; i < indices.size(); ++i)
    slices << ((i == x || i == y) ? ":"
                                  : QString("%1:%2")
                                        .arg(indices[i].toLongLong())
                                        .arg(indices[i].toLongLong() + 1));
  auto view = a.array->GetView(("[" + slices.join(',') + "]").toStdString());
  if (!view)
    return {nullptr, GDALClose};
  Dataset classic(view->AsClassicDataset(x, y), GDALClose);
  if (!classic)
    return {nullptr, GDALClose};
  // An in-memory VRT owns a reference to the sliced dataset. No source file is
  // modified.
  auto vrt =
      new VRTDataset(classic->GetRasterXSize(), classic->GetRasterYSize());
  vrt->AddBand(classic->GetRasterBand(1)->GetRasterDataType(), nullptr);
  auto band = static_cast<VRTSourcedRasterBand *>(vrt->GetRasterBand(1));
  band->AddSimpleSource(classic->GetRasterBand(1));
  bool hasND = false;
  double nd = a.array->GetNoDataValueAsDouble(&hasND);
  if (hasND)
    band->SetNoDataValue(nd);
  auto [scale, offset] = calibration(a.array);
  band->SetScale(scale);
  band->SetOffset(offset);
  band->SetUnitType(a.array->GetUnit().c_str());
  double gt[6];
  if (classic->GetGeoTransform(gt) == CE_None)
    static_cast<GDALDataset *>(vrt)->SetGeoTransform(gt);
  if (auto spatial = classic->GetSpatialRef())
    vrt->SetSpatialRef(spatial);
  else {
    auto xc = a.array->GetDimensions()[x]->GetIndexingVariable(),
         yc = a.array->GetDimensions()[y]->GetIndexingVariable();
    auto unit = [](const std::shared_ptr<GDALMDArray> &coord) {
      if (!coord)
        return QString();
      QString u = QString::fromStdString(coord->GetUnit());
      return u.isEmpty() ? attribute(coord, "units") : u;
    };
    if (xc && yc && unit(xc).toLower().contains("degrees_east") &&
        unit(yc).toLower().contains("degrees_north")) {
      OGRSpatialReference wgs;
      wgs.SetWellKnownGeogCS("WGS84");
      wgs.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
      vrt->SetSpatialRef(&wgs);
    }
  }
  classic.release()->ReleaseRef();
  return Dataset(vrt, GDALClose);
}
QVariantMap preview(const QString &path) {
  std::lock_guard lock(ioMutex());
  auto ds = open(path);
  if (!ds || ds->GetRasterCount() < 1)
    return {{"error", "无法读取切片"}};
  int w = ds->GetRasterXSize(), h = ds->GetRasterYSize();
  double ratio = std::min(1.0, 800.0 / std::max(w, h));
  int ow = std::max(1, int(w * ratio)), oh = std::max(1, int(h * ratio));
  std::vector<double> values(ow * oh);
  auto b = ds->GetRasterBand(1);
  if (b->RasterIO(GF_Read, 0, 0, w, h, values.data(), ow, oh, GDT_Float64, 0, 0,
                  nullptr) != CE_None)
    return {{"error", "切片读取失败"}};
  int hasND = 0;
  double nd = b->GetNoDataValue(&hasND);
  auto [scale, offset] = scaleOffset(path);
  auto ok = [&](double v) {
    return std::isfinite(v) &&
           !(hasND && (v == nd || (std::isnan(v) && std::isnan(nd))));
  };
  double lo = std::numeric_limits<double>::infinity(), hi = -lo;
  for (double v : values)
    if (ok(v)) {
      v = v * scale + offset;
      lo = std::min(lo, v);
      hi = std::max(hi, v);
    }
  const auto selection = decode(path);
  const auto display = selection.value("display").toMap();
  double displayLo = lo, displayHi = hi;
  if (display.value("fixed").toBool()) {
    displayLo = display.value("minimum").toDouble();
    displayHi = display.value("maximum").toDouble();
  }
  QVariantList histogram;
  std::array<int, 32> bins{};
  double mean = 0;
  int validCount = 0;
  for (double v : values)
    if (ok(v)) {
      v = v * scale + offset;
      mean += v;
      ++validCount;
      int bin =
          hi > lo ? std::clamp(int((v - lo) / (hi - lo) * 32), 0, 31) : 16;
      ++bins[bin];
    }
  for (int count : bins)
    histogram << count;
  QImage image(ow, oh, QImage::Format_ARGB32);
  image.fill(Qt::transparent);
  for (int i = 0; i < ow * oh; ++i)
    if (ok(values[i])) {
      double f = displayHi > displayLo
                     ? (values[i] * scale + offset - displayLo) /
                           (displayHi - displayLo)
                     : .5;
      image.setPixelColor(
          i % ow, i / ow,
          ColorRamp::color(f, display.value("ramp", "Viridis").toString(),
                           display.value("reversed").toBool()));
    }
  QByteArray png;
  QBuffer buffer(&png);
  buffer.open(QIODevice::WriteOnly);
  image.save(&buffer, "PNG");
  double gt[6];
  const bool geo = ds->GetSpatialRef() && ds->GetGeoTransform(gt) == CE_None;
  QImage legend(256, 12, QImage::Format_RGB32);
  for (int x = 0; x < 256; ++x)
    for (int y = 0; y < 12; ++y)
      legend.setPixelColor(
          x, y,
          ColorRamp::color(x / 255.,
                           display.value("ramp", "Viridis").toString(),
                           display.value("reversed").toBool()));
  QByteArray legendPng;
  QBuffer legendBuffer(&legendPng);
  legendBuffer.open(QIODevice::WriteOnly);
  legend.save(&legendBuffer, "PNG");
  QString time;
  auto a = arrayFor(selection);
  int td = selection.value("time", -1).toInt();
  if (a.array && td >= 0) {
    auto coordinate = a.array->GetDimensions()[td]->GetIndexingVariable();
    auto index = selection.value("indices").toList()[td].toULongLong();
    time = QString::number(index);
    if (coordinate && coordinate->GetDimensionCount() == 1) {
      GUInt64 st = index;
      size_t ct = 1;
      double value;
      if (coordinate->Read(&st, &ct, nullptr, nullptr,
                           GDALExtendedDataType::Create(GDT_Float64), &value)) {
        QString u = QString::fromStdString(coordinate->GetUnit());
        if (u.isEmpty())
          u = attribute(coordinate, "units");
        time = timeLabel(value, u, attribute(coordinate, "calendar"));
      }
    }
  }
  return {
      {"legend",
       "data:image/png;base64," + QString::fromLatin1(legendPng.toBase64())},
      {"timeLabel", time},
      {"histogram", histogram},
      {"sampleCount", int(values.size())},
      {"validCount", validCount},
      {"mean", validCount ? QVariant(mean / validCount) : QVariant()},
      {"displayMinimum",
       std::isfinite(displayLo) ? QVariant(displayLo) : QVariant()},
      {"displayMaximum",
       std::isfinite(displayHi) ? QVariant(displayHi) : QVariant()},
      {"image", "data:image/png;base64," + QString::fromLatin1(png.toBase64())},
      {"width", w},
      {"height", h},
      {"geographic", geo},
      {"minimum", std::isfinite(lo) ? QVariant(lo) : QVariant()},
      {"maximum", std::isfinite(hi) ? QVariant(hi) : QVariant()}};
}
QVariantMap series(const QString &path, double x, double y, bool geographic,
                   const QString &profile) {
  std::lock_guard lock(ioMutex());
  auto s = decode(path);
  auto a = arrayFor(s);
  if (!valid(s, a.array))
    return {{"error", "无效的变量或维度选择"}};
  int xd = s.value("x").toInt(), yd = s.value("y").toInt(),
      td = s.value("time", -1).toInt();

  if (geographic) {
    auto ds = open(path);
    double gt[6], inv[6];
    if (!ds || !ds->GetSpatialRef() || ds->GetGeoTransform(gt) != CE_None ||
        !GDALInvGeoTransform(gt, inv))
      return {{"error", "缺少地理参考，请在像素视图中点击"}};
    OGRSpatialReference source;
    source.SetWellKnownGeogCS("WGS84");
    source.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    auto target =
        std::unique_ptr<OGRSpatialReference>(ds->GetSpatialRef()->Clone());
    target->SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    auto tr =
        std::unique_ptr<OGRCoordinateTransformation,
                        decltype(&OGRCoordinateTransformation::DestroyCT)>(
            OGRCreateCoordinateTransformation(&source, target.get()),
            OGRCoordinateTransformation::DestroyCT);
    if (!tr || !tr->Transform(1, &x, &y))
      return {{"error", "坐标转换失败"}};
    double px = inv[0] + inv[1] * x + inv[2] * y;
    y = inv[3] + inv[4] * x + inv[5] * y;
    x = px;
  }
  const auto &dims = a.array->GetDimensions();
  if (!std::isfinite(x) || !std::isfinite(y) || x < 0 || y < 0 ||
      x >= double(dims[xd]->GetSize()) || y >= double(dims[yd]->GetSize()))
    return {{"error", "点击位置不在变量范围内"}};
  const int originalTime = td;
  if (profile == "row")
    td = xd;
  else if (profile == "column")
    td = yd;
  QVariantList cells;
  const auto fixed = s.value("indices").toList();
  for (int row = std::max(0, int(y) - 2);
       row <= std::min(int(dims[yd]->GetSize()) - 1, int(y) + 2); ++row) {
    for (int col = std::max(0, int(x) - 2);
         col <= std::min(int(dims[xd]->GetSize()) - 1, int(x) + 2); ++col) {
      std::vector<GUInt64> st(dims.size());
      std::vector<size_t> ct(dims.size(), 1);
      for (size_t i = 0; i < dims.size(); ++i)
        st[i] = fixed[int(i)].toULongLong();
      st[xd] = col;
      st[yd] = row;
      double value = 0;
      bool read =
          a.array->Read(st.data(), ct.data(), nullptr, nullptr,
                        GDALExtendedDataType::Create(GDT_Float64), &value);
      bool has = false;
      double nd = a.array->GetNoDataValueAsDouble(&has);
      bool ok = read && std::isfinite(value) && !(has && value == nd);
      double physical =
          value * calibration(a.array).first + calibration(a.array).second;
      cells << QVariantMap{{"column", col},
                           {"row", row},
                           {"raw", ok ? QVariant(value) : QVariant()},
                           {"value", ok && std::isfinite(physical)
                                         ? QVariant(physical)
                                         : QVariant()}};
    }
  }
  if (td < 0)
    return {{"cells", cells},
            {"pixel", QString("%1, %2").arg(int(x)).arg(int(y))},
            {"unit", QString::fromStdString(a.array->GetUnit())},
            {"variable", s.value("array")},
            {"points", QVariantList()},
            {"message", "二维变量：可查看邻域数值或行/列剖面"}};
  size_t n = dims[td]->GetSize();
  if (n > 1000000)
    return {{"error", "时间维度超过 100 万点，请先对子集进行处理"}};
  std::vector<GUInt64> start(dims.size());
  std::vector<size_t> count(dims.size(), 1);
  auto indices = s.value("indices").toList();
  for (size_t i = 0; i < dims.size(); ++i)
    start[i] = indices[int(i)].toULongLong();
  start[xd] = GUInt64(x);
  start[yd] = GUInt64(y);
  start[td] = 0;
  count[td] = n;
  std::vector<double> values(n), times(n);
  std::iota(times.begin(), times.end(), 0.0);
  if (!a.array->Read(start.data(), count.data(), nullptr, nullptr,
                     GDALExtendedDataType::Create(GDT_Float64), values.data()))
    return {{"error", "时序读取失败"}};
  auto coordinate = dims[td]->GetIndexingVariable();
  QString units, calendar;
  Q_UNUSED(originalTime);
  if (coordinate && coordinate->GetDimensionCount() == 1) {
    GUInt64 st = 0;
    size_t ct = n;
    if (coordinate->Read(&st, &ct, nullptr, nullptr,
                         GDALExtendedDataType::Create(GDT_Float64),
                         times.data())) {
      units = QString::fromStdString(coordinate->GetUnit());
      if (units.isEmpty())
        units = attribute(coordinate, "units");
      calendar = attribute(coordinate, "calendar");
    }
  }
  bool hasND = false;
  double nd = a.array->GetNoDataValueAsDouble(&hasND);
  auto [scale, offset] = calibration(a.array);
  QVariantList points;
  size_t good = 0;
  for (size_t i = 0; i < n; ++i) {
    bool ok = std::isfinite(values[i]) && !(hasND && values[i] == nd);
    double v = values[i] * scale + offset;
    ok = ok && std::isfinite(v);
    if (ok)
      ++good;
    points.push_back(
        QVariantMap{{"x", std::isfinite(times[i]) ? times[i] : double(i)},
                    {"label", timeLabel(times[i], units, calendar)},
                    {"value", ok ? QVariant(v) : QVariant()}});
  }
  return {{"points", points},
          {"pixel", QString("%1, %2").arg(int(x)).arg(int(y))},
          {"cells", cells},
          {"profile", profile},
          {"unit", QString::fromStdString(a.array->GetUnit())},
          {"timeUnit", units},
          {"calendar", calendar},
          {"validCount", int(good)},
          {"variable", s.value("array")}};
}
} // namespace ScientificData

QByteArray ScientificData::csv(const QVariantMap &result) {
  auto quote = [](QString s) {
    s.replace('"', "\"\"");
    return '"' + s + '"';
  };
  QString text = "variable,pixel,coordinate,label,value,unit,calendar\r\n";
  for (const auto &item : result.value("points").toList()) {
    auto p = item.toMap();
    text += quote(result.value("variable").toString()) + "," +
            quote(result.value("pixel").toString()) + "," +
            QString::number(p.value("x").toDouble(), 'g', 17) + "," +
            quote(p.value("label").toString()) + "," +
            (p.value("value").isNull()
                 ? QString()
                 : QString::number(p.value("value").toDouble(), 'g', 17)) +
            "," + quote(result.value("unit").toString()) + "," +
            quote(result.value("calendar").toString()) + "\r\n";
  }
  return text.toUtf8();
}
