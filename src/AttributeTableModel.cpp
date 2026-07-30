#include "AttributeTableModel.h"

#include <QLocale>

#include <gdal_priv.h>
#include <ogrsf_frmts.h>

#include <algorithm>
#include <memory>
#include <numeric>

namespace {

using GdalDatasetPtr = std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
using FeaturePtr =
    std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;

QString featureValue(const OGRFeature &feature, int field)
{
    if (!feature.IsFieldSetAndNotNull(field))
        return QStringLiteral("—");
    return QString::fromUtf8(feature.GetFieldAsString(field));
}

int compareValues(const QString &left, const QString &right)
{
    bool leftIsNumber = false;
    bool rightIsNumber = false;
    const double leftNumber = QLocale::c().toDouble(left, &leftIsNumber);
    const double rightNumber = QLocale::c().toDouble(right, &rightIsNumber);
    if (leftIsNumber && rightIsNumber) {
        if (leftNumber < rightNumber)
            return -1;
        if (leftNumber > rightNumber)
            return 1;
        return 0;
    }
    return QString::localeAwareCompare(left, right);
}

} // namespace

AttributeTableModel::AttributeTableModel(LayerModel *layers, QObject *parent)
    : QAbstractTableModel(parent),
      m_layers(layers)
{
}

int AttributeTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_visibleRows.size();
}

int AttributeTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_columns.size();
}

QVariant AttributeTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid()
        || (role != Qt::DisplayRole && role != DisplayRole)
        || index.row() < 0 || index.row() >= m_visibleRows.size()
        || index.column() < 0 || index.column() >= m_columns.size()) {
        return {};
    }

    const int sourceRow = m_visibleRows.at(index.row());
    return m_rows.at(sourceRow).at(index.column());
}

QVariant AttributeTableModel::headerData(int section,
                                         Qt::Orientation orientation,
                                         int role) const
{
    if (role != Qt::DisplayRole)
        return {};
    if (orientation == Qt::Horizontal
        && section >= 0 && section < m_columns.size()) {
        return m_columns.at(section);
    }
    return section + 1;
}

QHash<int, QByteArray> AttributeTableModel::roleNames() const
{
    return {{DisplayRole, "display"}};
}

bool AttributeTableModel::loadLayer(int row)
{
    clear();
    const LayerSnapshot *layer = m_layers ? m_layers->layerAt(row) : nullptr;
    if (!layer || layer->type != QStringLiteral("vector")) {
        setErrorMessage(tr("所选图层不是矢量图层"));
        return false;
    }

    // 属性表只在用户主动打开时读取。GDALDataset 与 OGRFeature 都由
    // unique_ptr 托管，任何提前返回都不会泄漏原生句柄。
    GdalDatasetPtr dataset(
        static_cast<GDALDataset *>(GDALOpenEx(
            layer->path.toUtf8().constData(),
            GDAL_OF_VECTOR | GDAL_OF_READONLY, nullptr, nullptr, nullptr)),
        GDALClose);
    if (!dataset) {
        setErrorMessage(tr("无法打开矢量数据源"));
        return false;
    }

    OGRLayer *source =
        dataset->GetLayerByName(layer->sourceLayer.toUtf8().constData());
    if (!source)
        source = dataset->GetLayer(0);
    if (!source) {
        setErrorMessage(tr("无法读取矢量图层"));
        return false;
    }

    const OGRFeatureDefn *definition = source->GetLayerDefn();
    if (!definition) {
        setErrorMessage(tr("矢量图层缺少字段定义"));
        return false;
    }

    beginResetModel();
    m_layerName = layer->name;
    m_columns.reserve(definition->GetFieldCount() + 1);
    m_columns.push_back(QStringLiteral("FID"));
    for (int field = 0; field < definition->GetFieldCount(); ++field) {
        m_columns.push_back(
            QString::fromUtf8(definition->GetFieldDefn(field)->GetNameRef()));
    }

    const auto featureCount = source->GetFeatureCount(false);
    if (featureCount > 0)
        m_rows.reserve(static_cast<qsizetype>(featureCount));
    // 将字段值缓存为 QString，TableView 滚动时不再访问 OGR 句柄。
    // 这样模型生命周期与数据集生命周期解耦，也便于安全地排序和筛选。
    source->ResetReading();
    while (true) {
        FeaturePtr feature(source->GetNextFeature(),
                           OGRFeature::DestroyFeature);
        if (!feature)
            break;
        QVector<QString> values;
        values.reserve(m_columns.size());
        values.push_back(QString::number(feature->GetFID()));
        for (int field = 0; field < definition->GetFieldCount(); ++field)
            values.push_back(featureValue(*feature, field));
        m_rows.push_back(std::move(values));
    }
    m_visibleRows.resize(m_rows.size());
    std::iota(m_visibleRows.begin(), m_visibleRows.end(), 0);
    endResetModel();

    emit layerChanged();
    emit countsChanged();
    setErrorMessage({});
    return true;
}

void AttributeTableModel::sortByColumn(int column, bool ascending)
{
    if (column < 0 || column >= m_columns.size())
        return;

    m_sortColumn = column;
    m_sortAscending = ascending;
    emit layoutAboutToBeChanged();
    sortVisibleRows();
    emit layoutChanged();
}

void AttributeTableModel::sortVisibleRows()
{
    if (m_sortColumn < 0 || m_sortColumn >= m_columns.size())
        return;
    // compareValues 会优先尝试 C locale 数值比较，避免字符串排序出现
    // 1、10、2；非数值字段再退回本地化文本比较。
    std::stable_sort(
        m_visibleRows.begin(), m_visibleRows.end(),
        [this](int leftRow, int rightRow) {
            const int result =
                compareValues(m_rows.at(leftRow).at(m_sortColumn),
                              m_rows.at(rightRow).at(m_sortColumn));
            return m_sortAscending ? result < 0 : result > 0;
        });
}

void AttributeTableModel::setFilter(int column, const QString &text)
{
    m_filterColumn =
        column >= 0 && column < m_columns.size() ? column : -1;
    m_filterText = text.trimmed();
    rebuildVisibleRows();
}

void AttributeTableModel::clear()
{
    beginResetModel();
    m_layerName.clear();
    m_columns.clear();
    m_rows.clear();
    m_visibleRows.clear();
    m_filterColumn = -1;
    m_filterText.clear();
    m_sortColumn = -1;
    m_sortAscending = true;
    endResetModel();
    emit layerChanged();
    emit countsChanged();
    setErrorMessage({});
}

void AttributeTableModel::rebuildVisibleRows()
{
    // m_visibleRows 只保存源行索引，不复制整行属性。筛选后仍可复用原始
    // 数据，并保持此前选择的排序规则。
    beginResetModel();
    m_visibleRows.clear();
    m_visibleRows.reserve(m_rows.size());
    for (int row = 0; row < m_rows.size(); ++row) {
        if (m_filterColumn < 0 || m_filterText.isEmpty()
            || m_rows.at(row).at(m_filterColumn).contains(
                m_filterText, Qt::CaseInsensitive)) {
            m_visibleRows.push_back(row);
        }
    }
    sortVisibleRows();
    endResetModel();
    emit countsChanged();
}

void AttributeTableModel::setErrorMessage(const QString &message)
{
    if (m_errorMessage == message)
        return;
    m_errorMessage = message;
    emit errorMessageChanged();
}
