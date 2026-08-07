#pragma once

#include "AttributeTableModel.h"
#include "LayerModel.h"

#include <QObject>
#include <QVariantList>

class AppController final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(LayerModel *layerModel READ layerModel CONSTANT)
    Q_PROPERTY(AttributeTableModel *attributeTableModel
               READ attributeTableModel CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontChanged)
    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY fontChanged)
    Q_PROPERTY(QString qtStyle READ qtStyle NOTIFY qtStyleChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(double toolBarOpacity READ toolBarOpacity WRITE setToolBarOpacity
               NOTIFY toolBarOpacityChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppController(QObject *parent = nullptr);

    LayerModel *layerModel() { return &m_layerModel; }
    AttributeTableModel *attributeTableModel() { return &m_attributeTableModel; }
    QString fontFamily() const { return m_fontFamily; }
    int fontSize() const { return m_fontSize; }
    QString qtStyle() const { return m_qtStyle; }
    QString language() const { return m_language; }
    double toolBarOpacity() const { return m_toolBarOpacity; }
    bool restartRequired() const { return m_restartRequired; }
    QString statusMessage() const { return m_statusMessage; }
    QString version() const;

    static QString savedOrPlatformStyle();
    static QString savedOrSystemLanguage();

    Q_INVOKABLE void openFiles();
    void loadFiles(const QStringList &paths);
    Q_INVOKABLE void setFontFamily(const QString &family);
    Q_INVOKABLE void setFontSize(int size);
    Q_INVOKABLE void setQtStyle(const QString &style);
    Q_INVOKABLE void setLanguage(const QString &language);
    Q_INVOKABLE void setToolBarOpacity(double opacity);
    Q_INVOKABLE QString shortcut(const QString &action) const;
    Q_INVOKABLE void setShortcut(const QString &action, const QString &sequence);
    Q_INVOKABLE void resetShortcuts();
    Q_INVOKABLE QVariantList queryRasters(double longitude, double latitude) const;
    Q_INVOKABLE QVariantMap queryVector(int row, double longitude, double latitude,
                                       double toleranceDegrees) const;
    Q_INVOKABLE QVariantMap layerMetadata(int row) const;

signals:
    void fontChanged();
    void qtStyleChanged();
    void languageChanged();
    void toolBarOpacityChanged();
    void restartRequiredChanged();
    void statusMessageChanged();
    void layerAdded(double minLon, double minLat, double maxLon, double maxLat);
    void shortcutsChanged();

private:
    void loadDataset(const QString &path);
    void addVectorLayers(const QString &path, const QString &datasetId,
                         const QString &datasetName);
    void addRasterLayer(const QString &path, const QString &datasetId,
                        const QString &datasetName);
    void setStatus(const QString &message);

    LayerModel m_layerModel;
    AttributeTableModel m_attributeTableModel;
    QString m_fontFamily;
    int m_fontSize = 13;
    QString m_qtStyle;
    QString m_language;
    double m_toolBarOpacity = 0.85;
    bool m_restartRequired = false;
    QString m_statusMessage;
};
