#include "AttributeTableModel.h"
#include <algorithm>
#include <memory>
#include <ogrsf_frmts.h>
QVariant AttributeTableModel::data(const QModelIndex &i, int role) const {
  if (role != Qt::DisplayRole || !i.isValid() || i.row() >= m_visible.size() ||
      i.column() >= m_columns.size())
    return {};
  return m_rows[m_visible[i.row()]][i.column()];
}
bool AttributeTableModel::loadLayer(int row) {
  auto l = m_layers->layerAt(row);
  if (!l || l->type != "vector")
    return false;
  auto ds = std::unique_ptr<GDALDataset, decltype(&GDALClose)>(
      static_cast<GDALDataset *>(GDALOpenEx(l->path.toUtf8().constData(),
                                            GDAL_OF_VECTOR, nullptr, nullptr,
                                            nullptr)),
      GDALClose);
  if (!ds)
    return false;
  auto layer = ds->GetLayerByName(l->sourceLayer.toUtf8().constData());
  if (!layer)
    return false;
  beginResetModel();
  m_rows.clear();
  m_visible.clear();
  m_columns.clear();
  m_name = l->name;
  auto def = layer->GetLayerDefn();
  for (int i = 0; i < def->GetFieldCount(); ++i)
    m_columns << QString::fromUtf8(def->GetFieldDefn(i)->GetNameRef());
  while (auto raw = layer->GetNextFeature()) {
    auto f = std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>(
        raw, OGRFeature::DestroyFeature);
    QVariantList values;
    for (int i = 0; i < def->GetFieldCount(); ++i)
      values << (f->IsFieldSetAndNotNull(i)
                     ? QVariant(QString::fromUtf8(f->GetFieldAsString(i)))
                     : QVariant());
    m_visible.push_back(m_rows.size());
    m_rows.push_back(values);
  }
  endResetModel();
  emit layerChanged();
  return true;
}
void AttributeTableModel::setFilter(int col, const QString &text) {
  beginResetModel();
  m_visible.clear();
  for (int i = 0; i < m_rows.size(); ++i)
    if (col < 0 || col >= m_columns.size() ||
        m_rows[i][col].toString().contains(text, Qt::CaseInsensitive))
      m_visible << i;
  endResetModel();
  emit layerChanged();
}
void AttributeTableModel::sortByColumn(int col, bool ascending) {
  if (col < 0 || col >= m_columns.size())
    return;
  beginResetModel();
  std::stable_sort(m_visible.begin(), m_visible.end(), [&](int a, int b) {
    QString x = m_rows[a][col].toString(), y = m_rows[b][col].toString();
    bool xo, yo;
    double xn = x.toDouble(&xo), yn = y.toDouble(&yo);
    int c = xo && yo ? (xn < yn   ? -1
                        : xn > yn ? 1
                                  : 0)
                     : QString::localeAwareCompare(x, y);
    return ascending ? c < 0 : c > 0;
  });
  endResetModel();
}
