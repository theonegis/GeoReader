#include "LayerModel.h"

#include <QFileInfo>
#include <QSet>
#include <QUuid>
#include <algorithm>
#include <cmath>

namespace {

QVariantList toVariantList(const QVector<double> &values)
{
    QVariantList result;
    result.reserve(values.size());
    for (const double value : values)
        result.push_back(value);
    return result;
}

} // namespace

LayerModel::LayerModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int LayerModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_layers.size();
}

QVariant LayerModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_layers.size())
        return {};

    const auto &layer = m_layers.at(index.row());
    switch (role) {
    case IdRole: return layer.id;
    case NameRole: return layer.name;
    case PathRole: return layer.path;
    case SourceLayerRole: return layer.sourceLayer;
    case TypeRole: return layer.type;
    case GeometryTypeRole: return layer.geometryType;
    case VisibleRole: return layer.visible;
    case OpacityRole: return layer.opacity;
    case LineColorRole: return layer.lineColor;
    case FillColorRole: return layer.fillColor;
    case LineWidthRole: return layer.lineWidth;
    case BandCountRole: return layer.bandCount;
    case RedBandRole: return layer.redBand;
    case GreenBandRole: return layer.greenBand;
    case BlueBandRole: return layer.blueBand;
    case GrayBandRole: return layer.grayBand;
    case RasterModeRole: return layer.rasterMode;
    case ColorRampRole: return layer.colorRamp;
    case ColorRampReversedRole: return layer.colorRampReversed;
    case StretchModeRole: return layer.stretchMode;
    case BandMinimumsRole: return toVariantList(layer.bandMinimums);
    case BandMaximumsRole: return toVariantList(layer.bandMaximums);
    case NoDataEnabledRole: return layer.noDataEnabled;
    case NoDataValueRole: return layer.noDataValue;
    case CrsRole: return layer.crsLabel.isEmpty() ? tr("未知坐标系")
                                                  : layer.crsLabel;
    case DatasetIdRole: return layer.datasetId;
    case DatasetNameRole: return layer.datasetName;
    case CoordinateModeRole: return layer.coordinateMode;
    case MultidimensionalArrayRole: return layer.multidimensionalArray;
    case MultidimensionalSliceRole: return layer.multidimensionalSlice;
    default: return {};
    }
}

bool LayerModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_layers.size())
        return false;

    auto &layer = m_layers[index.row()];
    switch (role) {
    case VisibleRole: layer.visible = value.toBool(); break;
    case OpacityRole: layer.opacity = std::clamp(value.toDouble(), 0.0, 1.0); break;
    case LineColorRole: layer.lineColor = value.value<QColor>(); break;
    case FillColorRole: layer.fillColor = value.value<QColor>(); break;
    case LineWidthRole: layer.lineWidth = std::clamp(value.toDouble(), 0.25, 12.0); break;
    case RedBandRole: layer.redBand = value.toInt(); break;
    case GreenBandRole: layer.greenBand = value.toInt(); break;
    case BlueBandRole: layer.blueBand = value.toInt(); break;
    case GrayBandRole: layer.grayBand = value.toInt(); break;
    case RasterModeRole: layer.rasterMode = value.toString(); break;
    case ColorRampRole: layer.colorRamp = value.toString(); break;
    case ColorRampReversedRole: layer.colorRampReversed = value.toBool(); break;
    case StretchModeRole: layer.stretchMode = value.toString(); break;
    case NoDataEnabledRole: layer.noDataEnabled = value.toBool(); break;
    case NoDataValueRole: layer.noDataValue = value.toString(); break;
    default: return false;
    }
    emit dataChanged(index, index, {role});
    advanceRevision();
    emit renderingChanged();
    return true;
}

Qt::ItemFlags LayerModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                           : Qt::NoItemFlags;
}

QHash<int, QByteArray> LayerModel::roleNames() const
{
    return {
        {IdRole, "layerId"},
        {NameRole, "name"},
        {PathRole, "path"},
        {SourceLayerRole, "sourceLayer"},
        {TypeRole, "layerType"},
        {GeometryTypeRole, "geometryType"},
        {VisibleRole, "layerVisible"},
        {OpacityRole, "layerOpacity"},
        {LineColorRole, "lineColor"},
        {FillColorRole, "fillColor"},
        {LineWidthRole, "lineWidth"},
        {BandCountRole, "bandCount"},
        {RedBandRole, "redBand"},
        {GreenBandRole, "greenBand"},
        {BlueBandRole, "blueBand"},
        {GrayBandRole, "grayBand"},
        {RasterModeRole, "rasterMode"},
        {ColorRampRole, "colorRamp"},
        {ColorRampReversedRole, "colorRampReversed"},
        {StretchModeRole, "stretchMode"},
        {BandMinimumsRole, "bandMinimums"},
        {BandMaximumsRole, "bandMaximums"},
        {NoDataEnabledRole, "noDataEnabled"},
        {NoDataValueRole, "noDataValue"},
        {CrsRole, "crs"},
        {DatasetIdRole, "datasetId"},
        {DatasetNameRole, "datasetName"},
        {CoordinateModeRole, "coordinateMode"},
        {MultidimensionalArrayRole, "multidimensionalArray"},
        {MultidimensionalSliceRole, "multidimensionalSlice"}
    };
}

int LayerModel::datasetCount() const
{
    QSet<QString> datasets;
    datasets.reserve(m_layers.size());
    for (const auto &layer : m_layers)
        datasets.insert(layer.datasetId);
    return datasets.size();
}

void LayerModel::addLayer(LayerSnapshot layer)
{
    if (layer.id.isEmpty())
        layer.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (layer.datasetId.isEmpty())
        layer.datasetId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (layer.datasetName.isEmpty())
        layer.datasetName = QFileInfo(layer.path).completeBaseName();
    const bool newDataset = std::none_of(
        m_layers.cbegin(), m_layers.cend(), [&layer](const LayerSnapshot &item) {
            return item.datasetId == layer.datasetId;
        });
    const int row = m_layers.size();
    beginInsertRows({}, row, row);
    m_layers.push_back(std::move(layer));
    endInsertRows();
    emit countChanged();
    if (newDataset)
        emit datasetCountChanged();
    advanceRevision();
    emit renderingChanged();
}

const LayerSnapshot *LayerModel::layerAt(int row) const
{
    return row >= 0 && row < m_layers.size() ? &m_layers.at(row) : nullptr;
}

QVector<LayerSnapshot> LayerModel::snapshots() const
{
    return m_layers;
}

QVariantMap LayerModel::get(int row) const
{
    QVariantMap result;
    if (row < 0 || row >= m_layers.size())
        return result;
    const QModelIndex idx = index(row);
    const auto roles = roleNames();
    for (auto it = roles.cbegin(); it != roles.cend(); ++it)
        result.insert(QString::fromUtf8(it.value()), data(idx, it.key()));
    const auto &layer = m_layers.at(row);
    result.insert(QStringLiteral("minLon"), layer.minLon);
    result.insert(QStringLiteral("minLat"), layer.minLat);
    result.insert(QStringLiteral("maxLon"), layer.maxLon);
    result.insert(QStringLiteral("maxLat"), layer.maxLat);
    return result;
}

QVariantMap LayerModel::datasetInfo(const QString &datasetId) const
{
    QVariantMap result;
    int layerCount = 0;
    int visibleCount = 0;
    int firstRow = -1;
    for (int row = 0; row < m_layers.size(); ++row) {
        const auto &layer = m_layers.at(row);
        if (layer.datasetId != datasetId)
            continue;
        if (firstRow < 0) {
            firstRow = row;
            result.insert(QStringLiteral("id"), layer.datasetId);
            result.insert(QStringLiteral("name"), layer.datasetName);
            result.insert(QStringLiteral("path"), layer.path);
        }
        ++layerCount;
        if (layer.visible)
            ++visibleCount;
    }
    if (firstRow < 0)
        return {};
    result.insert(QStringLiteral("firstRow"), firstRow);
    result.insert(QStringLiteral("layerCount"), layerCount);
    result.insert(QStringLiteral("visibleCount"), visibleCount);
    result.insert(QStringLiteral("allVisible"), visibleCount == layerCount);
    result.insert(QStringLiteral("anyVisible"), visibleCount > 0);
    return result;
}

void LayerModel::setVisible(int row, bool visible)
{
    setData(index(row), visible, VisibleRole);
}

void LayerModel::setOpacity(int row, double opacity)
{
    setData(index(row), opacity, OpacityRole);
}

void LayerModel::setVectorStyle(int row, const QColor &lineColor,
                                const QColor &fillColor, double lineWidth)
{
    if (row < 0 || row >= m_layers.size())
        return;
    auto &layer = m_layers[row];
    layer.lineColor = lineColor;
    layer.fillColor = fillColor;
    layer.lineWidth = std::clamp(lineWidth, 0.25, 12.0);
    emit dataChanged(index(row), index(row), {LineColorRole, FillColorRole, LineWidthRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::setRasterStyle(int row, const QString &mode, int redBand,
                                int greenBand, int blueBand, int grayBand,
                                const QString &colorRamp,
                                bool colorRampReversed,
                                const QString &stretchMode)
{
    if (row < 0 || row >= m_layers.size())
        return;
    auto &layer = m_layers[row];
    const auto clampBand = [&layer](int band) {
        return std::clamp(band, 1, std::max(1, layer.bandCount));
    };
    layer.rasterMode = mode;
    layer.redBand = clampBand(redBand);
    layer.greenBand = clampBand(greenBand);
    layer.blueBand = clampBand(blueBand);
    layer.grayBand = clampBand(grayBand);
    layer.colorRamp = colorRamp;
    layer.colorRampReversed = colorRampReversed;
    const QStringList supportedStretchModes {
        QStringLiteral("minmax"),
        QStringLiteral("percent_clip"),
        QStringLiteral("standard_deviation"),
        QStringLiteral("histogram_equalization")
    };
    layer.stretchMode = supportedStretchModes.contains(stretchMode)
        ? stretchMode : QStringLiteral("minmax");
    emit dataChanged(index(row), index(row),
                     {RasterModeRole, RedBandRole, GreenBandRole, BlueBandRole,
                      GrayBandRole, ColorRampRole, ColorRampReversedRole,
                      StretchModeRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::setBandRange(int row, int band, double minimum,
                              double maximum)
{
    if (row < 0 || row >= m_layers.size() || !std::isfinite(minimum)
        || !std::isfinite(maximum) || minimum >= maximum)
        return;

    auto &layer = m_layers[row];
    const int indexInLayer = band - 1;
    if (indexInLayer < 0 || indexInLayer >= layer.bandMinimums.size()
        || indexInLayer >= layer.bandMaximums.size())
        return;

    layer.bandMinimums[indexInLayer] = minimum;
    layer.bandMaximums[indexInLayer] = maximum;
    emit dataChanged(index(row), index(row),
                     {BandMinimumsRole, BandMaximumsRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::setBandRanges(const QString &layerId,
                               const QVector<double> &minimums,
                               const QVector<double> &maximums)
{
    if (minimums.size() != maximums.size())
        return;
    const auto iterator =
        std::find_if(m_layers.begin(), m_layers.end(),
                     [&layerId](const LayerSnapshot &layer) {
                         return layer.id == layerId;
                     });
    if (iterator == m_layers.end()
        || iterator->bandMinimums.size() != minimums.size())
        return;
    const int row = static_cast<int>(std::distance(m_layers.begin(), iterator));
    iterator->bandMinimums = minimums;
    iterator->bandMaximums = maximums;
    emit dataChanged(index(row), index(row),
                     {BandMinimumsRole, BandMaximumsRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::setRasterNoData(int row, bool enabled, const QString &value)
{
    if (row < 0 || row >= m_layers.size())
        return;

    const QString normalized = value.trimmed().toLower();
    bool numericValue = false;
    normalized.toDouble(&numericValue);
    if (enabled && normalized != QStringLiteral("nan") && !numericValue)
        return;

    auto &layer = m_layers[row];
    layer.noDataEnabled = enabled;
    layer.noDataValue = normalized.isEmpty() ? QStringLiteral("nan")
                                              : normalized;
    emit dataChanged(index(row), index(row),
                     {NoDataEnabledRole, NoDataValueRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::moveLayer(int from, int to)
{
    if (from < 0 || from >= m_layers.size() || to < 0 || to >= m_layers.size() || from == to)
        return;
    // beginMoveRows 的目标位置使用“插入前”坐标；向下移动时需跨过源行。
    // QML、选择索引和渲染快照因此会收到标准模型移动通知，而非整表重置。
    const int destination = to > from ? to + 1 : to;
    beginMoveRows({}, from, from, {}, destination);
    m_layers.move(from, to);
    endMoveRows();
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::removeLayer(int row)
{
    if (row < 0 || row >= m_layers.size())
        return;
    const QString datasetId = m_layers.at(row).datasetId;
    const bool removesDataset = std::count_if(
        m_layers.cbegin(), m_layers.cend(), [&datasetId](const auto &layer) {
            return layer.datasetId == datasetId;
        }) == 1;
    beginRemoveRows({}, row, row);
    m_layers.removeAt(row);
    endRemoveRows();
    emit countChanged();
    if (removesDataset)
        emit datasetCountChanged();
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::removeDataset(const QString &datasetId)
{
    if (datasetId.isEmpty())
        return;

    bool removed = false;
    for (int end = m_layers.size() - 1; end >= 0;) {
        if (m_layers.at(end).datasetId != datasetId) {
            --end;
            continue;
        }
        int first = end;
        while (first > 0 && m_layers.at(first - 1).datasetId == datasetId)
            --first;
        beginRemoveRows({}, first, end);
        m_layers.erase(m_layers.begin() + first, m_layers.begin() + end + 1);
        endRemoveRows();
        removed = true;
        end = first - 1;
    }
    if (!removed)
        return;

    emit countChanged();
    emit datasetCountChanged();
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::setDatasetVisible(const QString &datasetId, bool visible)
{
    int first = -1;
    int last = -1;
    bool changed = false;
    for (int row = 0; row < m_layers.size(); ++row) {
        auto &layer = m_layers[row];
        if (layer.datasetId != datasetId)
            continue;
        first = first < 0 ? row : first;
        last = row;
        if (layer.visible != visible) {
            layer.visible = visible;
            changed = true;
        }
    }
    if (!changed)
        return;

    emit dataChanged(index(first), index(last), {VisibleRole});
    advanceRevision();
    emit renderingChanged();
}

void LayerModel::advanceRevision()
{
    ++m_revision;
    emit revisionChanged();
}
