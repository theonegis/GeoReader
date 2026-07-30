#pragma once

#include "LayerModel.h"

#include <QAbstractTableModel>
#include <QStringList>
#include <QVector>

class AttributeTableModel final : public QAbstractTableModel
{
    Q_OBJECT
    Q_PROPERTY(QString layerName READ layerName NOTIFY layerChanged)
    Q_PROPERTY(QStringList columns READ columns NOTIFY layerChanged)
    Q_PROPERTY(int totalCount READ totalCount NOTIFY countsChanged)
    Q_PROPERTY(int filteredCount READ filteredCount NOTIFY countsChanged)
    Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorMessageChanged)

public:
    enum Role {
        DisplayRole = Qt::UserRole + 1
    };
    Q_ENUM(Role)

    explicit AttributeTableModel(LayerModel *layers, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int columnCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString layerName() const { return m_layerName; }
    QStringList columns() const { return m_columns; }
    int totalCount() const { return m_rows.size(); }
    int filteredCount() const { return m_visibleRows.size(); }
    QString errorMessage() const { return m_errorMessage; }

    Q_INVOKABLE bool loadLayer(int row);
    Q_INVOKABLE void sortByColumn(int column, bool ascending);
    Q_INVOKABLE void setFilter(int column, const QString &text);
    Q_INVOKABLE void clear();

signals:
    void layerChanged();
    void countsChanged();
    void errorMessageChanged();

private:
    void rebuildVisibleRows();
    void sortVisibleRows();
    void setErrorMessage(const QString &message);

    LayerModel *m_layers = nullptr;
    QString m_layerName;
    QStringList m_columns;
    QVector<QVector<QString>> m_rows;
    QVector<int> m_visibleRows;
    int m_filterColumn = -1;
    QString m_filterText;
    int m_sortColumn = -1;
    bool m_sortAscending = true;
    QString m_errorMessage;
};
