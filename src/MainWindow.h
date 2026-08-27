#pragma once

#include <QHash>
#include <QMainWindow>
#include <QPointer>
#include <QVariantMap>

class AppController;
class MapCanvas;
class QAction;
class QCheckBox;
class QComboBox;
class QDialog;
class QDoubleSpinBox;
class QFormLayout;
class QFrame;
class QKeySequenceEdit;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QResizeEvent;
class QSlider;
class QSpinBox;
class QStackedWidget;
class QTableView;
class QTimer;
class QToolBar;
class QToolButton;
class QTreeWidget;
class QWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    enum class Panel { None, Layers, Raster, Vector, Settings };

    explicit MainWindow(AppController *controller, QWidget *parent = nullptr);

    MapCanvas *mapCanvas() const { return m_canvas; }
    void setActivePanel(const QString &panel);
    void selectVectorFeature(int row, double longitude, double latitude);
    void showMetadata(int row);
    void showAttributeTable(int row);

protected:
    void changeEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void buildActions();
    void buildToolRail();
    void buildFloatingPanel();
    void buildMapOverlays();
    void rebuildFloatingPanelForLanguage();
    QWidget *buildLayersPanel();
    QWidget *buildRasterPanel();
    QWidget *buildVectorPanel();
    QWidget *buildSettingsPanel();
    void showMultidimensionalImportDialog();
    void retranslateUi();
    void refreshThemedIcons();
    void positionFloatingUi();
    void updatePanelHeader();
    void applyShortcuts();
    void showPanel(Panel panel);
    void refreshLayerList();
    void refreshLayerEditor();
    void applyLayerEditor();
    void updateVectorLayerChoices();
    void updateVectorResult(const QVariantMap &result);
    void updateRasterResults();
    void updateCoordinates();
    void fitSelectedLayer();
    void removeSelectedLayer();
    void moveSelectedLayer(int offset);
    void moveLayer(int from, int to);
    static int remappedIndex(int current, int from, int to);
    int selectedLayerRow() const;
    int firstVisibleVectorLayer() const;
    void pickVectorColor(bool lineColor);
    void rebuildBandControls(int bandCount);
    void updateBandRangeEditors(QComboBox *combo, QDoubleSpinBox *minimum,
                                QDoubleSpinBox *maximum);
    static QString coordinateText(double value, bool longitude);

    AppController *m_controller = nullptr;
    MapCanvas *m_canvas = nullptr;
    Panel m_activePanel = Panel::None;
    int m_selectedLayer = -1;
    int m_selectedVectorLayer = -1;
    bool m_refreshingLayerEditor = false;

    QAction *m_openAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_panAction = nullptr;
    QAction *m_rectangleZoomAction = nullptr;
    QHash<QString, QAction *> m_shortcutActions;

    QWidget *m_mapHost = nullptr;
    QToolBar *m_toolRail = nullptr;
    QToolButton *m_layersButton = nullptr;
    QToolButton *m_rasterButton = nullptr;
    QToolButton *m_vectorButton = nullptr;
    QToolButton *m_settingsButton = nullptr;
    QFrame *m_panelFrame = nullptr;
    QLabel *m_panelTitle = nullptr;
    QLabel *m_panelSubtitle = nullptr;
    QToolButton *m_panelCloseButton = nullptr;
    QStackedWidget *m_panels = nullptr;
    QWidget *m_layersPanel = nullptr;
    QWidget *m_rasterPanel = nullptr;
    QWidget *m_vectorPanel = nullptr;
    QWidget *m_settingsPanel = nullptr;
    QWidget *m_emptyState = nullptr;
    QLabel *m_emptyIcon = nullptr;
    QFrame *m_coordinateFrame = nullptr;

    QListWidget *m_layerList = nullptr;
    QComboBox *m_baseMapCombo = nullptr;
    QWidget *m_layerEditor = nullptr;
    QLabel *m_layerIdentity = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_opacityValueLabel = nullptr;
    QPushButton *m_lineColorButton = nullptr;
    QPushButton *m_fillColorButton = nullptr;
    QDoubleSpinBox *m_lineWidthSpin = nullptr;
    QComboBox *m_stretchCombo = nullptr;
    QComboBox *m_redBandCombo = nullptr;
    QComboBox *m_greenBandCombo = nullptr;
    QComboBox *m_blueBandCombo = nullptr;
    QComboBox *m_grayBandCombo = nullptr;
    QComboBox *m_rampCombo = nullptr;
    QCheckBox *m_reverseRampCheck = nullptr;
    QCheckBox *m_noDataCheck = nullptr;
    QLineEdit *m_noDataEdit = nullptr;
    QDoubleSpinBox *m_redMinimumSpin = nullptr;
    QDoubleSpinBox *m_redMaximumSpin = nullptr;
    QDoubleSpinBox *m_greenMinimumSpin = nullptr;
    QDoubleSpinBox *m_greenMaximumSpin = nullptr;
    QDoubleSpinBox *m_blueMinimumSpin = nullptr;
    QDoubleSpinBox *m_blueMaximumSpin = nullptr;
    QDoubleSpinBox *m_grayMinimumSpin = nullptr;
    QDoubleSpinBox *m_grayMaximumSpin = nullptr;
    QWidget *m_rgbBandBox = nullptr;
    QWidget *m_singleBandBox = nullptr;
    QWidget *m_vectorStyleBox = nullptr;
    QWidget *m_rasterStyleBox = nullptr;
    QFormLayout *m_vectorStyleForm = nullptr;
    QFormLayout *m_rasterStyleForm = nullptr;
    QPushButton *m_attributeButton = nullptr;

    QTreeWidget *m_rasterResults = nullptr;
    QLabel *m_rasterCoordinate = nullptr;
    QTimer *m_rasterTimer = nullptr;
    double m_lastRasterLongitude = 0.0;
    double m_lastRasterLatitude = 0.0;
    bool m_hasLastRasterCoordinate = false;
    QListWidget *m_vectorLayerList = nullptr;
    QLabel *m_vectorHint = nullptr;
    QTreeWidget *m_vectorResults = nullptr;

    QComboBox *m_languageCombo = nullptr;
    QComboBox *m_fontCombo = nullptr;
    QSpinBox *m_fontSizeSpin = nullptr;
    QSlider *m_toolbarOpacitySlider = nullptr;
    QComboBox *m_themeCombo = nullptr;
    QLabel *m_versionLabel = nullptr;
    QLabel *m_copyrightLabel = nullptr;
    QHash<QString, QKeySequenceEdit *> m_shortcutEdits;

    QLabel *m_coordinateLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_renderingLabel = nullptr;
    QTimer *m_statusHideTimer = nullptr;
    QPointer<QDialog> m_attributeDialog;
    QPointer<QDialog> m_multidimensionalDialog;
    bool m_panelRebuildScheduled = false;
};
