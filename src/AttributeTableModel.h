#pragma once
#include "LayerModel.h"
#include <QAbstractTableModel>
class AttributeTableModel : public QAbstractTableModel {
  Q_OBJECT
  Q_PROPERTY(QStringList columns READ columns NOTIFY layerChanged)
  Q_PROPERTY(QString layerName READ layerName NOTIFY layerChanged)
  Q_PROPERTY(int totalCount READ totalCount NOTIFY layerChanged)
  Q_PROPERTY(int filteredCount READ filteredCount NOTIFY layerChanged)
public:
  explicit AttributeTableModel(LayerModel *layers, QObject *parent = nullptr)
      : QAbstractTableModel(parent), m_layers(layers) {}
  QStringList columns() const { return m_columns; }
  QString layerName() const { return m_name; }
  int totalCount() const { return m_rows.size(); }
  int filteredCount() const { return m_visible.size(); }
  int rowCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : m_visible.size();
  }
  int columnCount(const QModelIndex &p = {}) const override {
    return p.isValid() ? 0 : m_columns.size();
  }
  QVariant data(const QModelIndex &i,
                int role = Qt::DisplayRole) const override;
  Q_INVOKABLE bool loadLayer(int row);
  Q_INVOKABLE void setFilter(int column, const QString &text);
  Q_INVOKABLE void sortByColumn(int column, bool ascending);
signals:
  void layerChanged();

private:
  LayerModel *m_layers;
  QStringList m_columns;
  QString m_name;
  QVector<QVariantList> m_rows;
  QVector<int> m_visible;
};
