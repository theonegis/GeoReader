#pragma once
#include <QColor>
#include <QHash>
#include <QVector>
#include <algorithm>
namespace ColorRamp {
inline QColor color(double value, const QString &name, bool reverse = false) {
  static const QHash<QString, QVector<QColor>> palettes{
      {"Viridis",
       {QColor("#440154"), QColor("#3b528b"), QColor("#21918c"),
        QColor("#5ec962"), QColor("#fde725")}},
      {"Plasma",
       {QColor("#0d0887"), QColor("#7e03a8"), QColor("#cc4778"),
        QColor("#f89540"), QColor("#f0f921")}},
      {"Inferno",
       {QColor("#000004"), QColor("#57106e"), QColor("#bc3754"),
        QColor("#f98e09"), QColor("#fcffa4")}},
      {"Magma",
       {QColor("#000004"), QColor("#51127c"), QColor("#b73779"),
        QColor("#fc8961"), QColor("#fcfdbf")}},
      {"Cividis",
       {QColor("#00224e"), QColor("#424e6c"), QColor("#7d7c78"),
        QColor("#bcae6c"), QColor("#fee838")}},
      {"Turbo",
       {QColor("#30123b"), QColor("#28bceb"), QColor("#a4fc3c"),
        QColor("#fb7e21"), QColor("#7a0403")}},
      {"Terrain",
       {QColor("#333399"), QColor("#00aaff"), QColor("#55aa55"),
        QColor("#aa8855"), QColor("#ffffff")}},
      {"Gray", {Qt::black, Qt::white}}};
  const auto colors = palettes.value(name, palettes.value("Viridis"));
  double f = std::clamp(reverse ? 1 - value : value, 0.0, 1.0),
         z = f * (colors.size() - 1);
  int k = std::min(int(z), int(colors.size() - 2));
  double q = z - k;
  auto a = colors[k], b = colors[k + 1];
  return QColor::fromRgbF(a.redF() * (1 - q) + b.redF() * q,
                          a.greenF() * (1 - q) + b.greenF() * q,
                          a.blueF() * (1 - q) + b.blueF() * q);
}
} // namespace ColorRamp
