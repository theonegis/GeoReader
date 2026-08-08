#pragma once

#include <QAbstractListModel>
#include <QColor>
#include <QVector>

struct LayerSnapshot
{
    QString id;
    QString datasetId;
    QString datasetName;
    QString name;
    QString path;
    QString sourceUri;
    QString sourceLayer;
    QString type;
    QString geometryType;
    QString srs;
    QString crsLabel;
    QString coordinateMode = QStringLiteral("geographic");
    QString multidimensionalArray;
    QString multidimensionalSlice;
    bool visible = true;
    double opacity = 1.0;
    QColor lineColor = QColor(QStringLiteral("#1976D2"));
    QColor fillColor = QColor(QStringLiteral("#64B5F6"));
    double lineWidth = 1.5;
    int bandCount = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    int redBand = 1;
    int greenBand = 2;
    int blueBand = 3;
    int grayBand = 1;
    QString rasterMode = QStringLiteral("rgb");
    QString colorRamp = QStringLiteral("Viridis");
    bool colorRampReversed = false;
    QString stretchMode = QStringLiteral("minmax");
    QVector<double> bandMinimums;
    QVector<double> bandMaximums;
    bool noDataEnabled = true;
    QString noDataValue = QStringLiteral("nan");
    double minLon = 0.0;
    double minLat = 0.0;
    double maxLon = 0.0;
    double maxLat = 0.0;
};

class LayerModel final : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int datasetCount READ datasetCount NOTIFY datasetCountChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY revisionChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        NameRole,
        PathRole,
        SourceLayerRole,
        TypeRole,
        GeometryTypeRole,
        VisibleRole,
        OpacityRole,
        LineColorRole,
        FillColorRole,
        LineWidthRole,
        BandCountRole,
        RedBandRole,
        GreenBandRole,
        BlueBandRole,
        GrayBandRole,
        RasterModeRole,
        ColorRampRole,
        ColorRampReversedRole,
        StretchModeRole,
        BandMinimumsRole,
        BandMaximumsRole,
        NoDataEnabledRole,
        NoDataValueRole,
        CrsRole,
        DatasetIdRole,
        DatasetNameRole,
        CoordinateModeRole,
        MultidimensionalArrayRole,
        MultidimensionalSliceRole
    };
    Q_ENUM(Role)

    explicit LayerModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    int count() const { return m_layers.size(); }
    int datasetCount() const;
    quint64 revision() const { return m_revision; }
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addLayer(LayerSnapshot layer);
    const LayerSnapshot *layerAt(int row) const;
    QVector<LayerSnapshot> snapshots() const;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE QVariantMap datasetInfo(const QString &datasetId) const;
    Q_INVOKABLE void setVisible(int row, bool visible);
    Q_INVOKABLE void setOpacity(int row, double opacity);
    Q_INVOKABLE void setVectorStyle(int row, const QColor &lineColor,
                                    const QColor &fillColor, double lineWidth);
    Q_INVOKABLE void setRasterStyle(int row, const QString &mode, int redBand,
                                    int greenBand, int blueBand, int grayBand,
                                    const QString &colorRamp,
                                    bool colorRampReversed,
                                    const QString &stretchMode);
    Q_INVOKABLE void setBandRange(int row, int band, double minimum,
                                  double maximum);
    void setBandRanges(const QString &layerId, const QVector<double> &minimums,
                       const QVector<double> &maximums);
    Q_INVOKABLE void setRasterNoData(int row, bool enabled,
                                     const QString &value);
    Q_INVOKABLE void moveLayer(int from, int to);
    Q_INVOKABLE void removeLayer(int row);
    Q_INVOKABLE void removeDataset(const QString &datasetId);
    Q_INVOKABLE void setDatasetVisible(const QString &datasetId, bool visible);

signals:
    void renderingChanged();
    void countChanged();
    void datasetCountChanged();
    void revisionChanged();

private:
    void advanceRevision();

    QVector<LayerSnapshot> m_layers;
    quint64 m_revision = 0;
};
