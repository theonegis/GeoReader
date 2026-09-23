#pragma once
#include <QString>
#include <QVariantMap>
#include <gdal_priv.h>
#include <memory>
#include <mutex>

namespace ScientificData {
// GDAL netCDF and HDF5 use different driver locks but share libhdf5.
// Serialize their accesses, including handle destruction, across worker
// threads.
std::recursive_mutex &ioMutex();
using Dataset = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
QVariantMap catalog(const QString &path);
QString encode(const QVariantMap &selection);
QVariantMap decode(const QString &path);
Dataset open(const QString &path);
std::pair<double, double> scaleOffset(const QString &path);
QVariantMap preview(const QString &path);
QVariantMap series(const QString &path, double x, double y, bool geographic,
                   const QString &profile = QStringLiteral("time"));
QByteArray csv(const QVariantMap &result);
} // namespace ScientificData
