#pragma once

#include "LayerModel.h"
#include "AttributeTableModel.h"

#include <QObject>
#include <QVariantList>

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(AttributeTableModel *attributeTableModel READ attributeTableModel CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(double toolBarOpacity READ toolBarOpacity WRITE setToolBarOpacity NOTIFY toolBarOpacityChanged)
    Q_PROPERTY(QVariantMap scientificCatalog READ scientificCatalog NOTIFY scientificCatalogChanged)
    Q_PROPERTY(QVariantMap scientificView READ scientificView NOTIFY scientificViewChanged)
    Q_PROPERTY(QVariantMap timeSeries READ timeSeries NOTIFY timeSeriesChanged)
    Q_PROPERTY(bool scientificBusy READ scientificBusy NOTIFY scientificBusyChanged)
    Q_PROPERTY(LayerModel *layerModel READ layerModel CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(QString qtStyle READ qtStyle NOTIFY qtStyleChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);

    AttributeTableModel *attributeTableModel() { return &m_attributeTable; }
    QString language() const { return m_language; }
    void setLanguage(const QString &language);
    double toolBarOpacity() const { return m_toolBarOpacity; }
    void setToolBarOpacity(double value);
    static QString savedOrSystemLanguage();
    QVariantMap scientificCatalog() const { return m_catalog; }
    QVariantMap scientificView() const { return m_scientificView; }
    QVariantMap timeSeries() const { return m_timeSeries; }
    bool scientificBusy() const { return m_scientificJobs > 0; }
    Q_INVOKABLE QVariantMap layerMetadata(int row) const;
    Q_INVOKABLE void selectScientificVariable(int variable, int x, int y, int time, const QVariantList &indices);
    Q_INVOKABLE void cancelScientificSelection();
    Q_INVOKABLE void inspectScientificLayer(const QString &id);
    Q_INVOKABLE void setScientificSlice(const QString &id, int dimension, int index);
    Q_INVOKABLE void requestTimeSeries(const QString &id, double x, double y, bool geographic = true, const QString &profile = QStringLiteral("time"));
    Q_INVOKABLE void setScientificDisplay(const QString &id, const QString &ramp, bool reversed, bool fixed, double minimum, double maximum);
    Q_INVOKABLE void exportTimeSeries();
    Q_INVOKABLE void exportScientificImage();
    LayerModel *layerModel() { return &m_layerModel; }
    QString fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }
    QString qtStyle() const { return m_qtStyle; }
    bool restartRequired() const { return m_restartRequired; }
    QString statusMessage() const { return m_statusMessage; }
    QString version() const;

    static QString savedOrPlatformStyle();

    Q_INVOKABLE void openFiles();
    void loadFiles(const QStringList &paths);
    Q_INVOKABLE void setFontFamily(const QString &family);
    Q_INVOKABLE void setFontSize(int size);
    Q_INVOKABLE void setQtStyle(const QString &style);
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE void setShortcut(const QString &action, const QString &sequence);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE QVariantList queryRasters(double longitude, double latitude) const;
    Q_INVOKABLE QVariantMap queryVector(int row, double longitude, double latitude,
                                       double toleranceDegrees) const;

signals:
    void scientificCatalogChanged();
    void scientificViewChanged();
    void timeSeriesChanged();
    void scientificBusyChanged();
    void languageChanged();
    void toolBarOpacityChanged();
    void fontChanged();
    void qtStyleChanged();
    void restartRequiredChanged();
    void statusMessageChanged();
    void layerAdded(double minLon, double minLat, double maxLon, double maxLat);
    void shortcutsChanged();

private:
    void loadDataset(const QString &path);
    void addVectorLayers(const QString &path);
    void addRasterLayer(const QString &path, const QString &name = {});
    void nextScientificFile();
    void scientificJobFinished();
    void setStatus(const QString &message);

    LayerModel m_layerModel;
    AttributeTableModel m_attributeTable{&m_layerModel};
    QString m_language = savedOrSystemLanguage();
    double m_toolBarOpacity = 0.85;
    QVariantMap m_catalog, m_scientificView, m_timeSeries;
    QStringList m_scientificQueue;
    bool m_catalogLoading = false;
    int m_scientificJobs = 0;
    quint64 m_previewGeneration = 0, m_seriesGeneration = 0;
    QString m_inspectedId;
    QString m_fontFamily;
    int m_fontSize = 13;
    QString m_qtStyle;
    bool m_restartRequired = false;
    QString m_statusMessage;
};
