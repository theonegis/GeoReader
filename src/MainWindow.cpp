#include "MainWindow.h"

#include "AppController.h"
#include "AttributeTableModel.h"
#include "LayerModel.h"
#include "MapCanvas.h"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDropEvent>
#include <QEvent>
#include <QFontComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QIcon>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSettings>
#include <QSignalBlocker>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <oclero/qlementine/style/QlementineStyle.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <memory>

namespace {

class LayerListWidget final : public QListWidget
{
public:
    using MoveHandler = std::function<void(int, int)>;

    explicit LayerListWidget(QWidget *parent = nullptr)
        : QListWidget(parent)
    {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(true);
        setDragDropMode(QAbstractItemView::InternalMove);
        setDefaultDropAction(Qt::MoveAction);
    }

    MoveHandler moveHandler;

protected:
    void dropEvent(QDropEvent *event) override
    {
        const int from = currentRow();
        int to = indexAt(event->position().toPoint()).row();
        if (to < 0)
            to = count() - 1;
        event->acceptProposedAction();
        if (moveHandler && from >= 0 && to >= 0 && from != to) {
            const MoveHandler handler = moveHandler;
            // rowsMoved 会重建本列表；等 dropEvent 退出后再改模型，避免在
            // QListView 的拖放栈中清空其内部项目。
            QTimer::singleShot(0, this, [handler, from, to] {
                handler(from, to);
            });
        }
    }
};

class FrostedToolBar final : public QToolBar
{
public:
    explicit FrostedToolBar(QWidget *backdrop, QWidget *parent = nullptr)
        : QToolBar(parent), m_backdrop(backdrop)
    {
        setAutoFillBackground(false);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath clipPath;
        clipPath.addRoundedRect(
            QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 9.0, 9.0);
        painter.setClipPath(clipPath);

        if (m_backdrop && m_backdrop->isVisible()) {
            const QPoint sourceTopLeft = mapTo(m_backdrop, QPoint(0, 0));
            const QPixmap source = m_backdrop->grab(
                QRect(sourceTopLeft, size()));
            if (!source.isNull()) {
                const QSize reducedSize(
                    std::max(2, source.width() / 9),
                    std::max(2, source.height() / 9));
                const QImage reduced = source.toImage().scaled(
                    reducedSize, Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
                const QImage blurred = reduced.scaled(
                    source.size(), Qt::IgnoreAspectRatio,
                    Qt::SmoothTransformation);
                painter.drawImage(rect(), blurred);
            }
        }

        const double requestedOpacity = property("glassOpacity").isValid()
            ? property("glassOpacity").toDouble() : 0.85;
        QColor glass = QApplication::palette().color(QPalette::Window);
        glass.setAlphaF(std::clamp(0.56 + requestedOpacity * 0.34,
                                   0.74, 0.90));
        painter.fillPath(clipPath, glass);
        QColor border = palette().color(QPalette::Midlight);
        border.setAlpha(190);
        painter.setClipping(false);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(Qt::NoBrush);
        painter.drawPath(clipPath);
        painter.end();
        QToolBar::paintEvent(event);
    }

private:
    QPointer<QWidget> m_backdrop;
};

class FloatingPanelFrame final : public QFrame
{
public:
    explicit FloatingPanelFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setAutoFillBackground(false);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QPen(palette().color(QPalette::Midlight), 1.0));
        painter.setBrush(palette().color(QPalette::Window));
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                9.0, 9.0);
        QFrame::paintEvent(event);
    }
};

class StyleCardFrame final : public QFrame
{
public:
    explicit StyleCardFrame(QWidget *parent = nullptr)
        : QFrame(parent)
    {
        setFrameShape(QFrame::NoFrame);
        setAutoFillBackground(false);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QFrame::paintEvent(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QColor border = palette().color(QPalette::Mid);
        border.setAlpha(145);
        QColor background = palette().color(QPalette::Button);
        background.setAlpha(105);
        painter.setPen(QPen(border, 1.0));
        painter.setBrush(background);
        painter.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5),
                                8.0, 8.0);
    }
};

QFrame *separator(QWidget *parent = nullptr)
{
    auto *line = new QFrame(parent);
    line->setFrameShape(QFrame::HLine);
    line->setFrameShadow(QFrame::Sunken);
    return line;
}

QLabel *mutedLabel(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    label->setWordWrap(true);
    label->setProperty("class", QStringLiteral("muted"));
    return label;
}

QLabel *sectionTitle(const QString &text, QWidget *parent = nullptr)
{
    auto *label = new QLabel(text, parent);
    QFont font = label->font();
    font.setWeight(QFont::DemiBold);
    label->setFont(font);
    return label;
}

QFont compactTableFont()
{
    QFont font = QApplication::font();
    if (font.pointSizeF() > 0.0)
        font.setPointSizeF(std::clamp(font.pointSizeF() * 0.72, 9.0, 12.5));
    else if (font.pixelSize() > 0)
        font.setPixelSize(std::clamp(
            static_cast<int>(std::lround(font.pixelSize() * 0.72)), 11, 17));
    return font;
}

void configureTableTypography(QAbstractItemView *view, QHeaderView *header)
{
    const QFont tableFont = compactTableFont();
    if (view)
        view->setFont(tableFont);
    if (!header)
        return;
    QFont font = tableFont;
    font.setWeight(QFont::Medium);
    header->setFont(font);
    // Qlementine may choose its own header metrics while drawing. The local
    // header rule makes the compact table tier explicit without leaking into
    // parent cards or controls such as QComboBox.
    const double size = font.pointSizeF() > 0.0
        ? font.pointSizeF() : 10.0;
    header->setStyleSheet(QStringLiteral(
        "QHeaderView::section { color: palette(text); font-size: %1pt; "
        "font-weight: 500; }")
        .arg(size, 0, 'f', 1));
}

QDoubleSpinBox *numberSpin(QWidget *parent = nullptr)
{
    auto *spin = new QDoubleSpinBox(parent);
    spin->setDecimals(6);
    spin->setRange(-1.0e15, 1.0e15);
    spin->setKeyboardTracking(false);
    spin->setMaximumWidth(120);
    return spin;
}

QIcon colorSwatch(const QColor &color)
{
    QPixmap pixmap(18, 18);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color.darker(135), 1));
    painter.setBrush(color);
    painter.drawRoundedRect(QRectF(1.0, 1.0, 16.0, 16.0), 4.0, 4.0);
    return QIcon(pixmap);
}

QIcon layerPreview(const LayerSnapshot &layer)
{
    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF tile(2.0, 2.0, 24.0, 24.0);
    painter.setPen(QPen(QColor(100, 116, 139, 96), 1.0));
    painter.setBrush(QColor(148, 163, 184, 28));
    painter.drawRoundedRect(tile, 6.0, 6.0);

    if (layer.type == QStringLiteral("raster")) {
        QLinearGradient gradient(tile.topLeft(), tile.bottomRight());
        gradient.setColorAt(0.0, QColor(QStringLiteral("#2475E9")));
        gradient.setColorAt(0.55, QColor(QStringLiteral("#36B37E")));
        gradient.setColorAt(1.0, QColor(QStringLiteral("#F3B33D")));
        painter.setPen(Qt::NoPen);
        painter.setBrush(gradient);
        painter.drawRoundedRect(tile.adjusted(5, 5, -5, -5), 3.0, 3.0);
    } else {
        const QColor stroke = layer.lineColor.isValid()
            ? layer.lineColor : QColor(QStringLiteral("#2475E9"));
        const QColor fill = layer.fillColor.isValid()
            ? layer.fillColor : QColor(stroke.red(), stroke.green(),
                                       stroke.blue(), 72);
        painter.setPen(QPen(stroke, 2.0, Qt::SolidLine,
                            Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(fill);
        if (layer.geometryType == QStringLiteral("point"))
            painter.drawEllipse(QPointF(14.0, 14.0), 5.0, 5.0);
        else if (layer.geometryType == QStringLiteral("polygon"))
            painter.drawPolygon(QPolygonF({QPointF(7, 19), QPointF(9, 9),
                                            QPointF(18, 7), QPointF(21, 17),
                                            QPointF(14, 21)}));
        else
            painter.drawPolyline(QPolygonF({QPointF(6, 19), QPointF(10, 10),
                                             QPointF(15, 16), QPointF(22, 7)}));
    }
    return QIcon(pixmap);
}

QString geometryLabel(const LayerSnapshot &layer)
{
    if (layer.type == QStringLiteral("raster"))
        return QObject::tr("栅格 · %1 波段").arg(layer.bandCount);
    const QString geometry = layer.geometryType == QStringLiteral("point")
        ? QObject::tr("点")
        : layer.geometryType == QStringLiteral("polygon")
            ? QObject::tr("面") : QObject::tr("线");
    return QObject::tr("矢量 · %1").arg(geometry);
}

QString panelTitle(MainWindow::Panel panel)
{
    switch (panel) {
    case MainWindow::Panel::Layers: return QObject::tr("图层");
    case MainWindow::Panel::Raster: return QObject::tr("栅格值");
    case MainWindow::Panel::Vector: return QObject::tr("矢量属性");
    case MainWindow::Panel::Settings: return QObject::tr("设置");
    case MainWindow::Panel::None: break;
    }
    return {};
}

QString shortcutLabel(const QString &key)
{
    if (key == QStringLiteral("open"))
        return QObject::tr("打开文件");
    if (key == QStringLiteral("zoomIn"))
        return QObject::tr("放大");
    if (key == QStringLiteral("zoomOut"))
        return QObject::tr("缩小");
    if (key == QStringLiteral("pan"))
        return QObject::tr("平移");
    if (key == QStringLiteral("fit"))
        return QObject::tr("适合范围");
    return key;
}

} // namespace

MainWindow::MainWindow(AppController *controller, QWidget *parent)
    : QMainWindow(parent), m_controller(controller)
{
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(QStringLiteral("GeoReader"));
    setMinimumSize(900, 600);
    resize(1280, 820);

    m_mapHost = new QWidget(this);
    m_mapHost->setObjectName(QStringLiteral("mapHost"));
    setCentralWidget(m_mapHost);

    m_canvas = new MapCanvas(m_mapHost);
    m_canvas->setObjectName(QStringLiteral("mapCanvas"));
    m_canvas->setLayerModel(m_controller->layerModel());

    buildActions();
    buildToolRail();
    buildFloatingPanel();
    buildMapOverlays();

    connect(m_controller, &AppController::canvasModeRequested,
            m_canvas, &MapCanvas::setCoordinateMode);
    connect(m_controller,
            &AppController::pendingMultidimensionalImportChanged,
            this, &MainWindow::showMultidimensionalImportDialog);

    connect(m_controller, &AppController::layerAdded, this,
            [this](double minLon, double minLat,
                   double maxLon, double maxLat) {
        m_canvas->fitBounds(minLon, minLat, maxLon, maxLat);
        if (m_selectedLayer < 0)
            m_selectedLayer = m_controller->layerModel()->count() - 1;
        m_hasLastRasterCoordinate = false;
        refreshLayerList();
        updateVectorLayerChoices();
    });
    connect(m_controller, &AppController::statusMessageChanged, this,
            [this] {
        m_statusLabel->setText(m_controller->statusMessage());
        m_statusLabel->adjustSize();
        m_statusLabel->show();
        positionFloatingUi();
        m_statusHideTimer->start();
    });
    connect(m_controller, &AppController::shortcutsChanged,
            this, &MainWindow::applyShortcuts);
    connect(m_controller, &AppController::toolBarOpacityChanged, this,
            [this] {
        m_toolRail->setProperty("glassOpacity",
                                m_controller->toolBarOpacity());
        m_toolRail->update();
    });
    connect(m_controller, &AppController::fontChanged, this, [this] {
        // 加粗标题和紧凑表头使用相对字号；字体变化后延迟重建，确保它们
        // 以新的应用字体为基准，同时避免在设置控件的信号栈内删除控件。
        if (!m_panelRebuildScheduled) {
            m_panelRebuildScheduled = true;
            QTimer::singleShot(0, this,
                               &MainWindow::rebuildFloatingPanelForLanguage);
        }
    });
    connect(m_controller->layerModel(), &QAbstractItemModel::dataChanged,
            this, [this] {
        m_hasLastRasterCoordinate = false;
        refreshLayerList();
        updateVectorLayerChoices();
    });
    connect(m_controller->layerModel(), &QAbstractItemModel::rowsRemoved,
            this, [this](const QModelIndex &, int first, int last) {
        const auto adjusted = [first, last](int current) {
            if (current < first)
                return current;
            if (current <= last)
                return -1;
            return current - (last - first + 1);
        };
        m_selectedLayer = adjusted(m_selectedLayer);
        m_selectedVectorLayer = adjusted(m_selectedVectorLayer);
        refreshLayerList();
        updateVectorLayerChoices();
        m_hasLastRasterCoordinate = false;
    });
    connect(m_controller->layerModel(), &QAbstractItemModel::rowsMoved,
            this, [this] {
        refreshLayerList();
        updateVectorLayerChoices();
        m_hasLastRasterCoordinate = false;
    });
    connect(m_canvas, &MapCanvas::mouseCoordinateChanged,
            this, &MainWindow::updateCoordinates);
    connect(m_canvas, &MapCanvas::viewportChanged,
            this, [this] { m_toolRail->update(); });
    connect(m_canvas, &MapCanvas::baseMapChanged, this, [this] {
        if (!m_baseMapCombo)
            return;
        const QSignalBlocker blocker(m_baseMapCombo);
        m_baseMapCombo->setCurrentIndex(std::max(
            0, m_baseMapCombo->findData(m_canvas->baseMap())));
    });
    connect(m_canvas, &MapCanvas::renderingChanged, this, [this] {
        m_renderingLabel->setText(
            m_canvas->rendering() ? tr("正在渲染…") : QString());
        m_toolRail->update();
    });
    connect(m_canvas, &MapCanvas::renderError, this,
            [this](const QString &message) {
        QMessageBox::warning(this, tr("地图渲染错误"), message);
    });
    connect(m_canvas, &MapCanvas::mapClicked, this,
            [this](double longitude, double latitude) {
        if (m_activePanel != Panel::Vector)
            return;
        int row = m_selectedVectorLayer;
        if (row < 0)
            row = firstVisibleVectorLayer();
        if (row < 0)
            return;
        selectVectorFeature(row, longitude, latitude);
    });

    m_statusLabel->setText(m_controller->statusMessage());
    applyShortcuts();
    refreshLayerList();
    updateVectorLayerChoices();
    retranslateUi();

    if (auto *qlementine = qobject_cast<
            oclero::qlementine::QlementineStyle *>(QApplication::style())) {
        connect(qlementine,
                &oclero::qlementine::QlementineStyle::themeChanged,
                this, &MainWindow::refreshThemedIcons);
    }
    refreshThemedIcons();
    QTimer::singleShot(0, this, &MainWindow::positionFloatingUi);
}

void MainWindow::buildActions()
{
    m_openAction = new QAction(this);
    m_openAction->setIcon(style()->standardIcon(QStyle::SP_DialogOpenButton));
    connect(m_openAction, &QAction::triggered,
            m_controller, &AppController::openFiles);

    m_zoomInAction = new QAction(this);
    m_zoomInAction->setIcon(style()->standardIcon(QStyle::SP_ArrowUp));
    connect(m_zoomInAction, &QAction::triggered,
            this, [this] { m_canvas->zoomBy(1.0); });

    m_zoomOutAction = new QAction(this);
    m_zoomOutAction->setIcon(style()->standardIcon(QStyle::SP_ArrowDown));
    connect(m_zoomOutAction, &QAction::triggered,
            this, [this] { m_canvas->zoomBy(-1.0); });

    m_fitAction = new QAction(this);
    m_fitAction->setIcon(style()->standardIcon(QStyle::SP_DesktopIcon));
    connect(m_fitAction, &QAction::triggered,
            this, &MainWindow::fitSelectedLayer);

    m_panAction = new QAction(this);
    m_panAction->setCheckable(true);
    m_panAction->setChecked(true);
    connect(m_panAction, &QAction::triggered, this, [this] {
        {
            const QSignalBlocker blocker(m_panAction);
            m_panAction->setChecked(true);
        }
        m_rectangleZoomAction->setChecked(false);
        showPanel(Panel::None);
        m_canvas->setInspectionMode(QStringLiteral("pan"));
        m_canvas->setFocus();
    });

    m_rectangleZoomAction = new QAction(this);
    m_rectangleZoomAction->setCheckable(true);
    connect(m_rectangleZoomAction, &QAction::toggled, this,
            [this](bool active) {
        if (active) {
            const QSignalBlocker blocker(m_panAction);
            m_panAction->setChecked(false);
        }
        m_canvas->setRectangleZoomActive(active);
    });
    connect(m_canvas, &MapCanvas::rectangleZoomActiveChanged, this, [this] {
        const bool active = m_canvas->rectangleZoomActive();
        {
            const QSignalBlocker blocker(m_rectangleZoomAction);
            m_rectangleZoomAction->setChecked(active);
        }
        if (!active && m_activePanel != Panel::Raster
            && m_activePanel != Panel::Vector) {
            const QSignalBlocker blocker(m_panAction);
            m_panAction->setChecked(true);
            m_canvas->setInspectionMode(QStringLiteral("pan"));
        }
    });

    m_shortcutActions = {
        {QStringLiteral("open"), m_openAction},
        {QStringLiteral("zoomIn"), m_zoomInAction},
        {QStringLiteral("zoomOut"), m_zoomOutAction},
        {QStringLiteral("fit"), m_fitAction},
        {QStringLiteral("pan"), m_panAction},
    };

    auto *escape = new QAction(this);
    escape->setShortcut(QKeySequence(Qt::Key_Escape));
    escape->setShortcutContext(Qt::ApplicationShortcut);
    addAction(escape);
    connect(escape, &QAction::triggered, this, [this] {
        m_canvas->setRectangleZoomActive(false);
    });
}

void MainWindow::buildToolRail()
{
    const QString toolBarStyle = QStringLiteral(
        "QToolBar { background: transparent; border: 0; "
        "padding: 5px; spacing: 4px; }"
        "QToolBar::separator { background: palette(midlight); margin: 4px 5px; "
        "height: 1px; width: 1px; }"
        "QToolButton { border: 0; border-radius: 7px; padding: 7px; }"
        "QToolButton:hover { background: rgba(127, 127, 127, 32); }"
        "QToolButton:checked { background: #2475E9; color: white; }");

    m_toolRail = new FrostedToolBar(m_canvas, m_mapHost);
    m_toolRail->setWindowTitle(tr("主工具栏"));
    m_toolRail->setObjectName(QStringLiteral("toolRail"));
    m_toolRail->setOrientation(Qt::Vertical);
    m_toolRail->setMovable(false);
    m_toolRail->setFloatable(false);
    m_toolRail->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_toolRail->setIconSize(QSize(24, 24));
    m_toolRail->setStyleSheet(toolBarStyle);
    m_toolRail->setProperty("glassOpacity", m_controller->toolBarOpacity());
    auto *shadow = new QGraphicsDropShadowEffect(m_toolRail);
    shadow->setBlurRadius(22.0);
    shadow->setOffset(0.0, 4.0);
    shadow->setColor(QColor(0, 0, 0, 45));
    m_toolRail->setGraphicsEffect(shadow);
    m_toolRail->addAction(m_openAction);
    if (auto *button = qobject_cast<QToolButton *>(
            m_toolRail->widgetForAction(m_openAction))) {
        button->setFixedSize(40, 40);
    }

    const auto addPanelButton = [this](QToolButton **target,
                                        QStyle::StandardPixmap icon,
                                        Panel panel) {
        auto *button = new QToolButton(m_toolRail);
        button->setCheckable(true);
        button->setAutoExclusive(false);
        button->setIcon(style()->standardIcon(icon));
        button->setFixedSize(40, 40);
        connect(button, &QToolButton::clicked, this,
                [this, panel] {
            showPanel(m_activePanel == panel ? Panel::None : panel);
        });
        m_toolRail->addWidget(button);
        *target = button;
    };
    addPanelButton(&m_layersButton, QStyle::SP_FileDialogDetailedView,
                   Panel::Layers);
    addPanelButton(&m_rasterButton, QStyle::SP_FileIcon, Panel::Raster);
    addPanelButton(&m_vectorButton, QStyle::SP_FileDialogContentsView,
                   Panel::Vector);

    auto *spacer = new QWidget(m_toolRail);
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    m_toolRail->addWidget(spacer);
    QAction *dataMapSeparator = m_toolRail->addSeparator();
    dataMapSeparator->setObjectName(QStringLiteral("dataMapSeparator"));
    const auto addNavigationAction = [this](QAction *action) {
        m_toolRail->addAction(action);
        if (auto *button = qobject_cast<QToolButton *>(
                m_toolRail->widgetForAction(action))) {
            button->setFixedSize(36, 36);
        }
    };
    addNavigationAction(m_zoomInAction);
    addNavigationAction(m_zoomOutAction);
    addNavigationAction(m_panAction);
    addNavigationAction(m_fitAction);
    addNavigationAction(m_rectangleZoomAction);
    QAction *mapSettingsSeparator = m_toolRail->addSeparator();
    mapSettingsSeparator->setObjectName(QStringLiteral("mapSettingsSeparator"));
    addPanelButton(&m_settingsButton, QStyle::SP_FileDialogInfoView,
                   Panel::Settings);
    refreshThemedIcons();
}

void MainWindow::buildFloatingPanel()
{
    m_panelFrame = new FloatingPanelFrame(m_mapHost);
    m_panelFrame->setObjectName(QStringLiteral("floatingPanel"));
    auto *shadow = new QGraphicsDropShadowEffect(m_panelFrame);
    shadow->setBlurRadius(28.0);
    shadow->setOffset(0.0, 6.0);
    shadow->setColor(QColor(0, 0, 0, 58));
    m_panelFrame->setGraphicsEffect(shadow);

    auto *panelLayout = new QVBoxLayout(m_panelFrame);
    panelLayout->setContentsMargins(0, 0, 0, 0);
    panelLayout->setSpacing(0);
    auto *header = new QWidget(m_panelFrame);
    header->setObjectName(QStringLiteral("floatingPanelHeader"));
    header->setFixedHeight(66);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 8, 10, 8);
    headerLayout->setSpacing(8);
    auto *titles = new QVBoxLayout;
    titles->setSpacing(1);
    m_panelTitle = new QLabel(header);
    m_panelTitle->setObjectName(QStringLiteral("floatingPanelTitle"));
    QFont titleFont = m_panelTitle->font();
    titleFont.setPointSizeF(titleFont.pointSizeF() + 1.5);
    titleFont.setWeight(QFont::DemiBold);
    m_panelTitle->setFont(titleFont);
    m_panelSubtitle = new QLabel(header);
    m_panelSubtitle->setObjectName(QStringLiteral("floatingPanelSubtitle"));
    m_panelSubtitle->setForegroundRole(QPalette::PlaceholderText);
    m_panelSubtitle->setSizePolicy(QSizePolicy::Expanding,
                                   QSizePolicy::Preferred);
    titles->addWidget(m_panelTitle);
    titles->addWidget(m_panelSubtitle);
    headerLayout->addLayout(titles, 1);
    m_panelCloseButton = new QToolButton(header);
    m_panelCloseButton->setObjectName(QStringLiteral("floatingPanelClose"));
    m_panelCloseButton->setAutoRaise(true);
    m_panelCloseButton->setFixedSize(32, 32);
    m_panelCloseButton->setIconSize(QSize(18, 18));
    connect(m_panelCloseButton, &QToolButton::clicked,
            this, [this] { showPanel(Panel::None); });
    headerLayout->addWidget(m_panelCloseButton, 0, Qt::AlignVCenter);
    panelLayout->addWidget(header);
    panelLayout->addWidget(separator(m_panelFrame));

    m_panels = new QStackedWidget(m_panelFrame);
    m_panels->setObjectName(QStringLiteral("floatingPanelPages"));
    m_layersPanel = buildLayersPanel();
    m_rasterPanel = buildRasterPanel();
    m_vectorPanel = buildVectorPanel();
    m_settingsPanel = buildSettingsPanel();
    m_panels->addWidget(m_layersPanel);
    m_panels->addWidget(m_rasterPanel);
    m_panels->addWidget(m_vectorPanel);
    m_panels->addWidget(m_settingsPanel);
    panelLayout->addWidget(m_panels, 1);
    m_panelFrame->hide();
    updatePanelHeader();
}

void MainWindow::buildMapOverlays()
{
    m_emptyState = new QWidget(m_mapHost);
    m_emptyState->setObjectName(QStringLiteral("emptyState"));
    auto *emptyLayout = new QVBoxLayout(m_emptyState);
    emptyLayout->setContentsMargins(8, 8, 8, 8);
    emptyLayout->setSpacing(10);
    emptyLayout->setAlignment(Qt::AlignCenter);

    auto *iconTile = new FloatingPanelFrame(m_emptyState);
    iconTile->setFixedSize(52, 52);
    auto *iconLayout = new QVBoxLayout(iconTile);
    iconLayout->setContentsMargins(13, 13, 13, 13);
    m_emptyIcon = new QLabel(iconTile);
    m_emptyIcon->setAlignment(Qt::AlignCenter);
    iconLayout->addWidget(m_emptyIcon);
    emptyLayout->addWidget(iconTile, 0, Qt::AlignHCenter);

    auto *title = new QLabel(tr("打开空间数据"), m_emptyState);
    title->setObjectName(QStringLiteral("emptyStateTitle"));
    QFont titleFont = title->font();
    titleFont.setPixelSize(17);
    titleFont.setWeight(QFont::DemiBold);
    title->setFont(titleFont);
    emptyLayout->addWidget(title, 0, Qt::AlignHCenter);
    auto *description = mutedLabel(
        tr("支持 Shapefile、GeoJSON、GeoPackage 和 GeoTIFF"), m_emptyState);
    description->setObjectName(QStringLiteral("emptyStateDescription"));
    emptyLayout->addWidget(description, 0, Qt::AlignHCenter);
    auto *open = new QPushButton(tr("选择文件…"), m_emptyState);
    open->setObjectName(QStringLiteral("emptyStateOpenButton"));
    connect(open, &QPushButton::clicked,
            m_controller, &AppController::openFiles);
    emptyLayout->addWidget(open, 0, Qt::AlignHCenter);
    m_emptyState->adjustSize();

    m_coordinateFrame = new FloatingPanelFrame(m_mapHost);
    m_coordinateFrame->setObjectName(QStringLiteral("coordinateBadge"));
    auto *coordinateLayout = new QHBoxLayout(m_coordinateFrame);
    coordinateLayout->setContentsMargins(10, 0, 10, 0);
    coordinateLayout->setSpacing(8);
    m_coordinateLabel = new QLabel(m_coordinateFrame);
    QFont coordinateFont = m_coordinateLabel->font();
    coordinateFont.setFamilies({QStringLiteral("Menlo"),
                                QStringLiteral("Monospace")});
    coordinateFont.setPixelSize(10);
    m_coordinateLabel->setFont(coordinateFont);
    coordinateLayout->addWidget(m_coordinateLabel);
    m_renderingLabel = new QLabel(m_coordinateFrame);
    m_renderingLabel->setForegroundRole(QPalette::PlaceholderText);
    coordinateLayout->addWidget(m_renderingLabel);
    m_coordinateFrame->setFixedHeight(30);

    m_statusLabel = new QLabel(m_mapHost);
    m_statusLabel->setObjectName(QStringLiteral("transientStatusBadge"));
    m_statusLabel->setContentsMargins(10, 5, 10, 5);
    m_statusLabel->setStyleSheet(
        QStringLiteral("QLabel { background: palette(window); "
                       "border: 1px solid palette(midlight); "
                       "border-radius: 7px; }"));
    m_statusLabel->hide();
    m_statusHideTimer = new QTimer(this);
    m_statusHideTimer->setSingleShot(true);
    m_statusHideTimer->setInterval(3000);
    connect(m_statusHideTimer, &QTimer::timeout,
            m_statusLabel, &QWidget::hide);
    updateCoordinates();
}

QWidget *MainWindow::buildLayersPanel()
{
    auto *page = new QWidget(m_panels);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *listSection = new StyleCardFrame(page);
    listSection->setObjectName(QStringLiteral("layerListSection"));
    auto *listSectionLayout = new QVBoxLayout(listSection);
    listSectionLayout->setContentsMargins(10, 8, 10, 10);
    listSectionLayout->setSpacing(6);
    auto *listTitle = new QLabel(tr("图层列表"), listSection);
    QFont listTitleFont = listTitle->font();
    listTitleFont.setWeight(QFont::DemiBold);
    listTitle->setFont(listTitleFont);
    listSectionLayout->addWidget(listTitle);

    auto *baseMapRow = new QHBoxLayout;
    baseMapRow->setSpacing(8);
    baseMapRow->addWidget(new QLabel(tr("底图"), listSection));
    m_baseMapCombo = new QComboBox(listSection);
    m_baseMapCombo->addItem(tr("OpenStreetMap"), QStringLiteral("osm"));
    m_baseMapCombo->addItem(tr("Esri 世界影像"),
                            QStringLiteral("esri_imagery"));
    m_baseMapCombo->addItem(tr("OpenTopoMap 地形图"),
                            QStringLiteral("opentopomap"));
    m_baseMapCombo->setCurrentIndex(std::max(
        0, m_baseMapCombo->findData(m_canvas->baseMap())));
    connect(m_baseMapCombo, qOverload<int>(&QComboBox::activated), this,
            [this](int index) {
        m_canvas->setBaseMap(
            m_baseMapCombo->itemData(index).toString());
    });
    baseMapRow->addWidget(m_baseMapCombo, 1);
    listSectionLayout->addLayout(baseMapRow);

    auto *list = new LayerListWidget(listSection);
    list->setObjectName(QStringLiteral("layerList"));
    list->setAlternatingRowColors(true);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->setMinimumHeight(150);
    list->setFrameShape(QFrame::NoFrame);
    list->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: 0; }"));
    list->moveHandler = [this](int from, int to) {
        moveLayer(from, to);
    };
    m_layerList = list;
    connect(m_layerList, &QListWidget::currentRowChanged, this,
            [this](int row) {
        m_selectedLayer = row;
        refreshLayerEditor();
    });
    connect(m_layerList, &QListWidget::itemChanged, this,
            [this](QListWidgetItem *item) {
        if (m_refreshingLayerEditor || !item)
            return;
        m_controller->layerModel()->setVisible(
            m_layerList->row(item), item->checkState() == Qt::Checked);
    });
    listSectionLayout->addWidget(m_layerList, 1);
    layout->addWidget(listSection, 2);

    auto *propertySection = new StyleCardFrame(page);
    propertySection->setObjectName(QStringLiteral("selectedLayerSection"));
    auto *propertySectionLayout = new QVBoxLayout(propertySection);
    propertySectionLayout->setContentsMargins(10, 8, 10, 10);
    propertySectionLayout->setSpacing(6);
    auto *propertyTitle = new QLabel(tr("所选图层属性"), propertySection);
    QFont propertyTitleFont = propertyTitle->font();
    propertyTitleFont.setWeight(QFont::DemiBold);
    propertyTitle->setFont(propertyTitleFont);
    propertySectionLayout->addWidget(propertyTitle);

    auto *scroll = new QScrollArea(propertySection);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setAutoFillBackground(false);
    m_layerEditor = new QWidget(scroll);
    m_layerEditor->setAutoFillBackground(false);
    auto *editor = new QVBoxLayout(m_layerEditor);
    editor->setContentsMargins(0, 0, 0, 0);
    m_layerIdentity = new QLabel(m_layerEditor);
    m_layerIdentity->setWordWrap(true);
    editor->addWidget(m_layerIdentity);

    auto *opacityRow = new QHBoxLayout;
    opacityRow->addWidget(new QLabel(tr("不透明度"), m_layerEditor));
    m_opacitySlider = new QSlider(Qt::Horizontal, m_layerEditor);
    m_opacitySlider->setRange(0, 100);
    opacityRow->addWidget(m_opacitySlider, 1);
    m_opacityValueLabel = new QLabel(m_layerEditor);
    m_opacityValueLabel->setMinimumWidth(38);
    m_opacityValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    opacityRow->addWidget(m_opacityValueLabel);
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int value) {
        if (!m_refreshingLayerEditor && selectedLayerRow() >= 0)
            m_controller->layerModel()->setOpacity(selectedLayerRow(), value / 100.0);
    });
    editor->addLayout(opacityRow);

    auto *vectorSection = new QWidget(m_layerEditor);
    vectorSection->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    auto *vectorSectionLayout = new QVBoxLayout(vectorSection);
    vectorSectionLayout->setContentsMargins(0, 0, 0, 0);
    vectorSectionLayout->setSpacing(6);
    auto *vectorTitle = new QLabel(tr("矢量样式"), vectorSection);
    QFont vectorTitleFont = vectorTitle->font();
    vectorTitleFont.setWeight(QFont::DemiBold);
    vectorTitle->setFont(vectorTitleFont);
    vectorSectionLayout->addWidget(vectorTitle);
    auto *vectorGroup = new StyleCardFrame(vectorSection);
    auto *vectorForm = new QFormLayout(vectorGroup);
    vectorForm->setContentsMargins(14, 12, 14, 12);
    m_vectorStyleForm = vectorForm;
    m_lineColorButton = new QPushButton(vectorGroup);
    m_fillColorButton = new QPushButton(vectorGroup);
    oclero::qlementine::QlementineStyle::setAutoIconColor(
        m_lineColorButton, oclero::qlementine::AutoIconColor::None);
    oclero::qlementine::QlementineStyle::setAutoIconColor(
        m_fillColorButton, oclero::qlementine::AutoIconColor::None);
    m_lineWidthSpin = new QDoubleSpinBox(vectorGroup);
    m_lineWidthSpin->setRange(0.25, 12.0);
    m_lineWidthSpin->setSingleStep(0.25);
    vectorForm->addRow(tr("线颜色"), m_lineColorButton);
    vectorForm->addRow(tr("填充 / 点颜色"), m_fillColorButton);
    vectorForm->addRow(tr("线宽"), m_lineWidthSpin);
    connect(m_lineColorButton, &QPushButton::clicked,
            this, [this] { pickVectorColor(true); });
    connect(m_fillColorButton, &QPushButton::clicked,
            this, [this] { pickVectorColor(false); });
    connect(m_lineWidthSpin,
            qOverload<double>(&QDoubleSpinBox::valueChanged),
            this, [this] { if (!m_refreshingLayerEditor) applyLayerEditor(); });
    vectorSectionLayout->addWidget(vectorGroup);
    m_vectorStyleBox = vectorSection;
    editor->addWidget(vectorSection);

    auto *rasterSection = new QWidget(m_layerEditor);
    rasterSection->setSizePolicy(QSizePolicy::Expanding,
                                 QSizePolicy::Preferred);
    auto *rasterSectionLayout = new QVBoxLayout(rasterSection);
    rasterSectionLayout->setContentsMargins(0, 0, 0, 0);
    rasterSectionLayout->setSpacing(6);
    auto *rasterTitle = new QLabel(tr("栅格显示"), rasterSection);
    QFont rasterTitleFont = rasterTitle->font();
    rasterTitleFont.setWeight(QFont::DemiBold);
    rasterTitle->setFont(rasterTitleFont);
    rasterSectionLayout->addWidget(rasterTitle);
    auto *rasterGroup = new StyleCardFrame(rasterSection);
    auto *rasterLayout = new QVBoxLayout(rasterGroup);
    rasterLayout->setContentsMargins(14, 12, 14, 12);
    auto *rasterForm = new QFormLayout;
    m_rasterStyleForm = rasterForm;
    m_stretchCombo = new QComboBox(rasterGroup);
    m_stretchCombo->addItem(tr("最小值–最大值"), QStringLiteral("minmax"));
    m_stretchCombo->addItem(tr("2%–98% 累计裁剪"), QStringLiteral("percent_clip"));
    m_stretchCombo->addItem(tr("均值 ±2σ"), QStringLiteral("standard_deviation"));
    m_stretchCombo->addItem(tr("直方图均衡"), QStringLiteral("histogram_equalization"));
    rasterForm->addRow(tr("拉伸方式"), m_stretchCombo);
    m_rampCombo = new QComboBox(rasterGroup);
    m_rampCombo->addItems({QStringLiteral("Viridis"), QStringLiteral("Plasma"),
                           QStringLiteral("Inferno"), QStringLiteral("Magma"),
                           QStringLiteral("Cividis"), QStringLiteral("Turbo"),
                           QStringLiteral("Terrain"), QStringLiteral("Gray")});
    rasterForm->addRow(tr("色带"), m_rampCombo);
    m_reverseRampCheck = new QCheckBox(tr("反转色带"), rasterGroup);
    rasterForm->addRow(QString(), m_reverseRampCheck);
    rasterLayout->addLayout(rasterForm);

    m_rgbBandBox = new QWidget(rasterGroup);
    auto *rgbGrid = new QGridLayout(m_rgbBandBox);
    rgbGrid->setContentsMargins(0, 0, 0, 0);
    rgbGrid->addWidget(new QLabel(tr("通道")), 0, 0);
    rgbGrid->addWidget(new QLabel(tr("波段")), 0, 1);
    rgbGrid->addWidget(new QLabel(tr("最小值")), 0, 2);
    rgbGrid->addWidget(new QLabel(tr("最大值")), 0, 3);
    const QStringList channels {tr("红"), tr("绿"), tr("蓝")};
    QComboBox **bandCombos[] {&m_redBandCombo, &m_greenBandCombo,
                              &m_blueBandCombo};
    QDoubleSpinBox **minimums[] {&m_redMinimumSpin, &m_greenMinimumSpin,
                                 &m_blueMinimumSpin};
    QDoubleSpinBox **maximums[] {&m_redMaximumSpin, &m_greenMaximumSpin,
                                 &m_blueMaximumSpin};
    for (int channel = 0; channel < 3; ++channel) {
        *bandCombos[channel] = new QComboBox(m_rgbBandBox);
        (*bandCombos[channel])->setMaximumWidth(100);
        *minimums[channel] = numberSpin(m_rgbBandBox);
        *maximums[channel] = numberSpin(m_rgbBandBox);
        rgbGrid->addWidget(new QLabel(channels.at(channel)), channel + 1, 0);
        rgbGrid->addWidget(*bandCombos[channel], channel + 1, 1);
        rgbGrid->addWidget(*minimums[channel], channel + 1, 2);
        rgbGrid->addWidget(*maximums[channel], channel + 1, 3);
        connect(*bandCombos[channel],
                qOverload<int>(&QComboBox::currentIndexChanged), this,
                [this, combo = *bandCombos[channel],
                 minimum = *minimums[channel],
                 maximum = *maximums[channel]] {
            updateBandRangeEditors(combo, minimum, maximum);
        });
        connect(*minimums[channel], &QDoubleSpinBox::editingFinished,
                this, [this] {
            if (!m_refreshingLayerEditor)
                applyLayerEditor();
        });
        connect(*maximums[channel], &QDoubleSpinBox::editingFinished,
                this, [this] {
            if (!m_refreshingLayerEditor)
                applyLayerEditor();
        });
    }
    rasterLayout->addWidget(m_rgbBandBox);

    m_singleBandBox = new QWidget(rasterGroup);
    auto *singleForm = new QFormLayout(m_singleBandBox);
    singleForm->setContentsMargins(0, 0, 0, 0);
    m_grayBandCombo = new QComboBox(m_singleBandBox);
    m_grayMinimumSpin = numberSpin(m_singleBandBox);
    m_grayMaximumSpin = numberSpin(m_singleBandBox);
    singleForm->addRow(tr("显示波段"), m_grayBandCombo);
    singleForm->addRow(tr("最小值"), m_grayMinimumSpin);
    singleForm->addRow(tr("最大值"), m_grayMaximumSpin);
    connect(m_grayBandCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this] {
        updateBandRangeEditors(m_grayBandCombo, m_grayMinimumSpin,
                               m_grayMaximumSpin);
    });
    connect(m_grayMinimumSpin, &QDoubleSpinBox::editingFinished,
            this, [this] {
        if (!m_refreshingLayerEditor)
            applyLayerEditor();
    });
    connect(m_grayMaximumSpin, &QDoubleSpinBox::editingFinished,
            this, [this] {
        if (!m_refreshingLayerEditor)
            applyLayerEditor();
    });
    rasterLayout->addWidget(m_singleBandBox);

    auto *noDataRow = new QHBoxLayout;
    m_noDataCheck = new QCheckBox(tr("使用 NoData"), rasterGroup);
    m_noDataEdit = new QLineEdit(rasterGroup);
    m_noDataEdit->setPlaceholderText(QStringLiteral("nan"));
    noDataRow->addWidget(m_noDataCheck);
    noDataRow->addWidget(m_noDataEdit, 1);
    rasterLayout->addLayout(noDataRow);
    const auto applyRasterStyle = [this] {
        if (!m_refreshingLayerEditor)
            applyLayerEditor();
    };
    connect(m_stretchCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            applyRasterStyle);
    connect(m_rampCombo,
            qOverload<int>(&QComboBox::currentIndexChanged), this,
            applyRasterStyle);
    connect(m_reverseRampCheck, &QCheckBox::toggled, this,
            applyRasterStyle);
    connect(m_noDataCheck, &QCheckBox::toggled, this,
            applyRasterStyle);
    connect(m_noDataCheck, &QCheckBox::toggled, m_noDataEdit,
            &QWidget::setEnabled);
    connect(m_noDataEdit, &QLineEdit::editingFinished, this,
            applyRasterStyle);
    rasterSectionLayout->addWidget(rasterGroup);
    m_rasterStyleBox = rasterSection;
    editor->addWidget(rasterSection);

    auto *detailsRow = new QHBoxLayout;
    auto *metadata = new QPushButton(tr("元数据"), m_layerEditor);
    auto *attributes = new QPushButton(tr("属性表"), m_layerEditor);
    m_attributeButton = attributes;
    auto *remove = new QToolButton(m_layerEditor);
    remove->setIcon(QIcon(QStringLiteral(":/icons/ui/trash.svg")));
    remove->setIconSize(QSize(18, 18));
    remove->setFixedSize(36, 36);
    remove->setToolTip(tr("移除图层"));
    remove->setProperty("class", QStringLiteral("danger"));
    connect(metadata, &QPushButton::clicked, this,
            [this] { showMetadata(selectedLayerRow()); });
    connect(attributes, &QPushButton::clicked, this,
            [this] { showAttributeTable(selectedLayerRow()); });
    connect(remove, &QPushButton::clicked,
            this, &MainWindow::removeSelectedLayer);
    detailsRow->addWidget(metadata);
    detailsRow->addWidget(attributes);
    metadata->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    attributes->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    detailsRow->addWidget(remove);
    editor->addLayout(detailsRow);
    editor->addStretch();
    scroll->setWidget(m_layerEditor);
    propertySectionLayout->addWidget(scroll, 1);
    layout->addWidget(propertySection, 3);
    return page;
}

QWidget *MainWindow::buildRasterPanel()
{
    auto *page = new QWidget(m_panels);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *locationSection = new StyleCardFrame(page);
    locationSection->setObjectName(QStringLiteral("rasterLocationSection"));
    auto *locationLayout = new QVBoxLayout(locationSection);
    locationLayout->setContentsMargins(10, 8, 10, 10);
    locationLayout->setSpacing(6);
    locationLayout->addWidget(sectionTitle(tr("查询位置"), locationSection));
    m_rasterCoordinate = mutedLabel({}, locationSection);
    locationLayout->addWidget(m_rasterCoordinate);
    locationLayout->addWidget(mutedLabel(
        tr("移动鼠标，实时读取所有可见栅格像元。"), locationSection));
    layout->addWidget(locationSection);

    auto *resultsSection = new StyleCardFrame(page);
    resultsSection->setObjectName(QStringLiteral("rasterResultsSection"));
    auto *resultsLayout = new QVBoxLayout(resultsSection);
    resultsLayout->setContentsMargins(10, 8, 10, 10);
    resultsLayout->setSpacing(6);
    resultsLayout->addWidget(sectionTitle(
        tr("可见栅格像元"), resultsSection));
    m_rasterResults = new QTreeWidget(resultsSection);
    m_rasterResults->setColumnCount(2);
    m_rasterResults->setRootIsDecorated(true);
    m_rasterResults->setAlternatingRowColors(true);
    m_rasterResults->setFrameShape(QFrame::NoFrame);
    m_rasterResults->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: transparent; border: 0; }"));
    m_rasterResults->header()->setStretchLastSection(true);
    configureTableTypography(m_rasterResults, m_rasterResults->header());
    resultsLayout->addWidget(m_rasterResults, 1);
    layout->addWidget(resultsSection, 1);
    m_rasterTimer = new QTimer(this);
    m_rasterTimer->setInterval(45);
    connect(m_rasterTimer, &QTimer::timeout,
            this, &MainWindow::updateRasterResults);
    return page;
}

QWidget *MainWindow::buildVectorPanel()
{
    auto *page = new QWidget(m_panels);
    auto *layout = new QVBoxLayout(page);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    auto *layerSection = new StyleCardFrame(page);
    layerSection->setObjectName(QStringLiteral("vectorLayerSection"));
    auto *layerLayout = new QVBoxLayout(layerSection);
    layerLayout->setContentsMargins(10, 8, 10, 10);
    layerLayout->setSpacing(6);
    layerLayout->addWidget(sectionTitle(tr("查询图层"), layerSection));
    m_vectorLayerList = new QListWidget(layerSection);
    m_vectorLayerList->setMaximumHeight(142);
    m_vectorLayerList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_vectorLayerList->setFrameShape(QFrame::NoFrame);
    m_vectorLayerList->setStyleSheet(QStringLiteral(
        "QListWidget { background: transparent; border: 0; }"));
    connect(m_vectorLayerList, &QListWidget::currentRowChanged, this,
            [this](int index) {
        const QListWidgetItem *item = m_vectorLayerList->item(index);
        m_selectedVectorLayer = item ? item->data(Qt::UserRole).toInt() : -1;
        m_canvas->clearSelectedFeature();
        updateVectorResult({});
    });
    layerLayout->addWidget(m_vectorLayerList);
    layout->addWidget(layerSection);

    auto *featureSection = new StyleCardFrame(page);
    featureSection->setObjectName(QStringLiteral("vectorFeatureSection"));
    auto *featureLayout = new QVBoxLayout(featureSection);
    featureLayout->setContentsMargins(10, 8, 10, 10);
    featureLayout->setSpacing(6);
    featureLayout->addWidget(sectionTitle(tr("要素属性"), featureSection));
    m_vectorHint = mutedLabel(tr("尚未识别要素"), featureSection);
    featureLayout->addWidget(m_vectorHint);
    m_vectorResults = new QTreeWidget(featureSection);
    m_vectorResults->setColumnCount(2);
    m_vectorResults->setAlternatingRowColors(true);
    m_vectorResults->setRootIsDecorated(false);
    m_vectorResults->setFrameShape(QFrame::NoFrame);
    m_vectorResults->setStyleSheet(QStringLiteral(
        "QTreeWidget { background: transparent; border: 0; }"));
    m_vectorResults->header()->setStretchLastSection(true);
    configureTableTypography(m_vectorResults, m_vectorResults->header());
    featureLayout->addWidget(m_vectorResults, 1);
    layout->addWidget(featureSection, 1);
    return page;
}

QWidget *MainWindow::buildSettingsPanel()
{
    auto *page = new QWidget(m_panels);
    auto *outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea(page);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *content = new QWidget(scroll);
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *appearance = new StyleCardFrame(content);
    appearance->setObjectName(QStringLiteral("appearanceSection"));
    auto *appearanceLayout = new QVBoxLayout(appearance);
    appearanceLayout->setContentsMargins(10, 8, 10, 10);
    appearanceLayout->setSpacing(6);
    appearanceLayout->addWidget(sectionTitle(tr("外观与主题"), appearance));
    auto *form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    m_languageCombo = new QComboBox(appearance);
    m_languageCombo->addItem(QStringLiteral("简体中文"), QStringLiteral("zh_CN"));
    m_languageCombo->addItem(QStringLiteral("English"), QStringLiteral("en_US"));
    m_languageCombo->setCurrentIndex(
        m_controller->language() == QStringLiteral("en_US") ? 1 : 0);
    connect(m_languageCombo,
            qOverload<int>(&QComboBox::activated), this, [this](int index) {
        m_controller->setLanguage(m_languageCombo->itemData(index).toString());
    });
    form->addRow(tr("语言"), m_languageCombo);
    auto *fontCombo = new QFontComboBox(appearance);
    fontCombo->setCurrentFont(QFont(m_controller->fontFamily()));
    m_fontCombo = fontCombo;
    connect(fontCombo, &QFontComboBox::currentFontChanged, this,
            [this](const QFont &font) {
        m_controller->setFontFamily(font.family());
    });
    form->addRow(tr("字体"), fontCombo);
    m_fontSizeSpin = new QSpinBox(appearance);
    m_fontSizeSpin->setRange(10, 18);
    m_fontSizeSpin->setValue(m_controller->fontSize());
    connect(m_fontSizeSpin,
            qOverload<int>(&QSpinBox::valueChanged),
            m_controller, &AppController::setFontSize);
    form->addRow(tr("字号"), m_fontSizeSpin);
    m_toolbarOpacitySlider = new QSlider(Qt::Horizontal, appearance);
    m_toolbarOpacitySlider->setRange(50, 100);
    m_toolbarOpacitySlider->setValue(
        static_cast<int>(m_controller->toolBarOpacity() * 100.0));
    connect(m_toolbarOpacitySlider, &QSlider::valueChanged, this,
            [this](int value) {
        m_controller->setToolBarOpacity(value / 100.0);
    });
    form->addRow(tr("工具栏磨砂浓度"), m_toolbarOpacitySlider);
    m_themeCombo = new QComboBox(appearance);
    m_themeCombo->addItem(tr("跟随系统"), QStringLiteral("System"));
    m_themeCombo->addItem(tr("浅色"), QStringLiteral("Light"));
    m_themeCombo->addItem(tr("深色"), QStringLiteral("Dark"));
    m_themeCombo->setCurrentIndex(std::max(
        0, m_themeCombo->findData(m_controller->qtStyle())));
    connect(m_themeCombo, qOverload<int>(&QComboBox::activated), this,
            [this](int index) {
        m_controller->setQtStyle(m_themeCombo->itemData(index).toString());
    });
    form->addRow(tr("Qlementine 主题"), m_themeCombo);
    appearanceLayout->addLayout(form);
    layout->addWidget(appearance);

    auto *shortcuts = new StyleCardFrame(content);
    shortcuts->setObjectName(QStringLiteral("shortcutsSection"));
    auto *shortcutsLayout = new QVBoxLayout(shortcuts);
    shortcutsLayout->setContentsMargins(10, 8, 10, 10);
    shortcutsLayout->setSpacing(6);
    shortcutsLayout->addWidget(sectionTitle(tr("快捷键"), shortcuts));
    auto *shortcutForm = new QFormLayout;
    shortcutForm->setContentsMargins(0, 0, 0, 0);
    const QStringList keys {QStringLiteral("open"), QStringLiteral("zoomIn"),
                            QStringLiteral("zoomOut"), QStringLiteral("pan"),
                            QStringLiteral("fit")};
    for (const QString &key : keys) {
        auto *edit = new QKeySequenceEdit(
            QKeySequence(m_controller->shortcut(key)), shortcuts);
        m_shortcutEdits.insert(key, edit);
        connect(edit, &QKeySequenceEdit::editingFinished, this,
                [this, key, edit] {
            if (!edit->keySequence().isEmpty())
                m_controller->setShortcut(
                    key, edit->keySequence().toString(QKeySequence::PortableText));
        });
        shortcutForm->addRow(shortcutLabel(key), edit);
    }
    auto *reset = new QPushButton(tr("恢复默认快捷键"), shortcuts);
    connect(reset, &QPushButton::clicked,
            m_controller, &AppController::resetShortcuts);
    shortcutForm->addRow(QString(), reset);
    shortcutsLayout->addLayout(shortcutForm);
    layout->addWidget(shortcuts);

    auto *about = new StyleCardFrame(content);
    about->setObjectName(QStringLiteral("aboutSection"));
    auto *aboutLayout = new QVBoxLayout(about);
    aboutLayout->setContentsMargins(10, 8, 10, 10);
    aboutLayout->setSpacing(6);
    aboutLayout->addWidget(sectionTitle(tr("关于"), about));
    m_versionLabel = mutedLabel({}, about);
    aboutLayout->addWidget(m_versionLabel);
    m_copyrightLabel = mutedLabel({}, about);
    aboutLayout->addWidget(m_copyrightLabel);
    layout->addWidget(about);
    layout->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll);
    return page;
}

void MainWindow::showMultidimensionalImportDialog()
{
    const QVariantMap pending =
        m_controller->pendingMultidimensionalImport();
    const QVariantList arrays =
        pending.value(QStringLiteral("arrays")).toList();
    if (arrays.isEmpty())
        return;
    if (m_multidimensionalDialog)
        m_multidimensionalDialog->close();

    auto *dialog = new QDialog(this);
    m_multidimensionalDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::WindowModal);
    dialog->setWindowTitle(tr("导入多维数据 — %1")
        .arg(pending.value(QStringLiteral("fileName")).toString()));
    dialog->resize(620, 520);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *summary = mutedLabel(
        tr("选择要显示的变量、X/Y 维以及其余维度的切片。"), dialog);
    layout->addWidget(summary);
    auto *form = new QFormLayout;
    auto *arrayCombo = new QComboBox(dialog);
    for (int index = 0; index < arrays.size(); ++index) {
        const QVariantMap array = arrays.at(index).toMap();
        arrayCombo->addItem(
            QStringLiteral("%1 · %2")
                .arg(array.value(QStringLiteral("name")).toString(),
                     array.value(QStringLiteral("dataType")).toString()),
            index);
    }
    form->addRow(tr("变量"), arrayCombo);
    auto *xCombo = new QComboBox(dialog);
    auto *yCombo = new QComboBox(dialog);
    form->addRow(tr("X 维"), xCombo);
    form->addRow(tr("Y 维"), yCombo);
    auto *coordinateMode = new QComboBox(dialog);
    coordinateMode->addItem(tr("自动识别"), QStringLiteral("auto"));
    coordinateMode->addItem(tr("地理坐标"), QStringLiteral("geographic"));
    coordinateMode->addItem(tr("投影坐标"), QStringLiteral("projected"));
    coordinateMode->addItem(tr("像素坐标"), QStringLiteral("pixel"));
    form->addRow(tr("坐标模式"), coordinateMode);
    auto *crs = new QLineEdit(dialog);
    crs->setPlaceholderText(QStringLiteral("EPSG:4326"));
    form->addRow(tr("源 CRS"), crs);
    layout->addLayout(form);

    auto *sliceSection = new StyleCardFrame(dialog);
    auto *sliceLayout = new QFormLayout(sliceSection);
    sliceLayout->setContentsMargins(12, 10, 12, 10);
    layout->addWidget(sliceSection, 1);
    auto sliceSpins = std::make_shared<QVector<QSpinBox *>>();

    const auto updateSliceAvailability =
        [xCombo, yCombo, sliceSpins] {
        const int x = xCombo->currentData().toInt();
        const int y = yCombo->currentData().toInt();
        for (int index = 0; index < sliceSpins->size(); ++index)
            sliceSpins->at(index)->setEnabled(index != x && index != y);
    };
    const auto updateArray =
        [arrays, arrayCombo, xCombo, yCombo, crs, sliceLayout,
         sliceSpins, updateSliceAvailability] {
        const int arrayIndex = arrayCombo->currentData().toInt();
        if (arrayIndex < 0 || arrayIndex >= arrays.size())
            return;
        const QVariantMap array = arrays.at(arrayIndex).toMap();
        const QVariantList dimensions =
            array.value(QStringLiteral("dimensions")).toList();
        const QSignalBlocker xBlocker(xCombo);
        const QSignalBlocker yBlocker(yCombo);
        xCombo->clear();
        yCombo->clear();
        while (QLayoutItem *item = sliceLayout->takeAt(0)) {
            if (item->widget())
                delete item->widget();
            delete item;
        }
        sliceSpins->clear();
        for (int index = 0; index < dimensions.size(); ++index) {
            const QVariantMap dimension = dimensions.at(index).toMap();
            const QString name = dimension.value(
                QStringLiteral("name")).toString();
            const qulonglong size = dimension.value(
                QStringLiteral("size")).toULongLong();
            const QString label = QStringLiteral("%1 (%2)").arg(name).arg(size);
            xCombo->addItem(label, index);
            yCombo->addItem(label, index);
            auto *spin = new QSpinBox(sliceLayout->parentWidget());
            spin->setRange(0, static_cast<int>(std::min<qulonglong>(
                size > 0 ? size - 1 : 0,
                static_cast<qulonglong>(std::numeric_limits<int>::max()))));
            sliceLayout->addRow(name, spin);
            sliceSpins->push_back(spin);
        }
        const int defaultX = array.value(
            QStringLiteral("defaultXDimension")).toInt();
        const int defaultY = array.value(
            QStringLiteral("defaultYDimension")).toInt();
        xCombo->setCurrentIndex(std::max(0, xCombo->findData(defaultX)));
        yCombo->setCurrentIndex(std::max(0, yCombo->findData(defaultY)));
        crs->setText(array.value(QStringLiteral("crs")).toString());
        updateSliceAvailability();
    };
    connect(arrayCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            dialog, [updateArray](int) { updateArray(); });
    connect(xCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            dialog, [updateSliceAvailability](int) {
        updateSliceAvailability();
    });
    connect(yCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            dialog, [updateSliceAvailability](int) {
        updateSliceAvailability();
    });
    updateArray();

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, dialog);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("导入"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("取消"));
    connect(buttons, &QDialogButtonBox::accepted, dialog,
            [this, dialog, arrayCombo, xCombo, yCombo, coordinateMode,
             crs, sliceSpins] {
        const int x = xCombo->currentData().toInt();
        const int y = yCombo->currentData().toInt();
        if (x == y) {
            QMessageBox::warning(dialog, tr("导入多维数据"),
                                 tr("X 维和 Y 维必须不同。"));
            return;
        }
        QVariantList slices;
        for (QSpinBox *spin : *sliceSpins)
            slices.push_back(spin->value());
        m_controller->confirmMultidimensionalImport(
            arrayCombo->currentData().toInt(), x, y, slices,
            coordinateMode->currentData().toString(), crs->text());
        dialog->accept();
    });
    connect(buttons, &QDialogButtonBox::rejected, dialog, [this, dialog] {
        m_controller->cancelMultidimensionalImport();
        dialog->reject();
    });
    layout->addWidget(buttons);
    dialog->show();
}

void MainWindow::showPanel(Panel panel)
{
    m_activePanel = panel;
    m_layersButton->setChecked(panel == Panel::Layers);
    m_rasterButton->setChecked(panel == Panel::Raster);
    m_vectorButton->setChecked(panel == Panel::Vector);
    m_settingsButton->setChecked(panel == Panel::Settings);
    const bool inspectionPanel = panel == Panel::Raster
        || panel == Panel::Vector;
    {
        const QSignalBlocker blocker(m_panAction);
        m_panAction->setChecked(!inspectionPanel);
    }
    m_canvas->setRectangleZoomActive(false);
    m_canvas->setInspectionMode(
        panel == Panel::Raster ? QStringLiteral("raster")
        : panel == Panel::Vector ? QStringLiteral("vector")
                                 : QStringLiteral("pan"));
    if (panel == Panel::None) {
        m_panelFrame->hide();
        m_rasterTimer->stop();
        m_canvas->clearSelectedFeature();
        positionFloatingUi();
        return;
    }
    QWidget *page = panel == Panel::Layers ? m_layersPanel
        : panel == Panel::Raster ? m_rasterPanel
        : panel == Panel::Vector ? m_vectorPanel : m_settingsPanel;
    m_panels->setCurrentWidget(page);
    updatePanelHeader();
    positionFloatingUi();
    m_panelFrame->show();
    m_panelFrame->raise();
    if (panel == Panel::Raster) {
        m_hasLastRasterCoordinate = false;
        updateRasterResults();
        m_rasterTimer->start();
    } else {
        m_rasterTimer->stop();
    }
    if (panel == Panel::Vector)
        updateVectorLayerChoices();
}

void MainWindow::setActivePanel(const QString &panel)
{
    const QString normalized = panel.toLower();
    showPanel(normalized == QStringLiteral("layers") ? Panel::Layers
              : normalized == QStringLiteral("raster") ? Panel::Raster
              : normalized == QStringLiteral("vector") ? Panel::Vector
              : normalized == QStringLiteral("settings") ? Panel::Settings
                                                          : Panel::None);
}

int MainWindow::selectedLayerRow() const
{
    return m_selectedLayer >= 0
            && m_selectedLayer < m_controller->layerModel()->count()
        ? m_selectedLayer : -1;
}

void MainWindow::refreshLayerList()
{
    if (!m_layerList)
        return;
    m_refreshingLayerEditor = true;
    const int previous = selectedLayerRow();
    m_layerList->clear();
    for (int row = 0; row < m_controller->layerModel()->count(); ++row) {
        const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
        auto *item = new QListWidgetItem(m_layerList);
        item->setText(QStringLiteral("%1\n%2 · %3")
                          .arg(layer->name, geometryLabel(*layer),
                               layer->crsLabel.isEmpty() ? tr("未知坐标系")
                                                         : layer->crsLabel));
        item->setCheckState(layer->visible ? Qt::Checked : Qt::Unchecked);
        item->setIcon(layerPreview(*layer));
        item->setFlags(item->flags() | Qt::ItemIsDragEnabled
                       | Qt::ItemIsDropEnabled | Qt::ItemIsUserCheckable);
        item->setToolTip(layer->path);
        item->setSizeHint(QSize(0, 54));
    }
    if (m_emptyState)
        m_emptyState->setVisible(m_controller->layerModel()->count() == 0);
    updatePanelHeader();
    if (previous >= 0)
        m_layerList->setCurrentRow(previous);
    m_refreshingLayerEditor = false;
    refreshLayerEditor();
}

void MainWindow::refreshLayerEditor()
{
    if (!m_layerEditor)
        return;
    const LayerSnapshot *layer =
        m_controller->layerModel()->layerAt(selectedLayerRow());
    m_layerEditor->setEnabled(layer != nullptr);
    if (!layer) {
        m_layerIdentity->setText(tr("选择一个图层以编辑样式"));
        return;
    }
    m_refreshingLayerEditor = true;
    m_layerIdentity->setText(
        QStringLiteral("<b>%1</b><br>%2 · %3")
            .arg(layer->name.toHtmlEscaped(), geometryLabel(*layer),
                 layer->crsLabel.toHtmlEscaped()));
    m_opacitySlider->setValue(static_cast<int>(layer->opacity * 100.0));
    m_opacityValueLabel->setText(
        QStringLiteral("%1%").arg(qRound(layer->opacity * 100.0)));
    m_vectorStyleBox->setVisible(layer->type == QStringLiteral("vector"));
    m_rasterStyleBox->setVisible(layer->type == QStringLiteral("raster"));
    m_attributeButton->setVisible(layer->type == QStringLiteral("vector"));
    if (m_activePanel == Panel::Layers)
        positionFloatingUi();
    if (layer->type == QStringLiteral("vector")) {
        const bool point = layer->geometryType == QStringLiteral("point");
        const bool polygon = layer->geometryType == QStringLiteral("polygon");
        m_vectorStyleForm->setRowVisible(m_lineColorButton, !point);
        m_vectorStyleForm->setRowVisible(m_fillColorButton, point || polygon);
        m_vectorStyleForm->setRowVisible(m_lineWidthSpin, !point);
        m_lineColorButton->setText(layer->lineColor.name());
        m_lineColorButton->setIcon(colorSwatch(layer->lineColor));
        m_fillColorButton->setText(layer->fillColor.name());
        m_fillColorButton->setIcon(colorSwatch(layer->fillColor));
        m_lineWidthSpin->setValue(layer->lineWidth);
    } else {
        rebuildBandControls(layer->bandCount);
        m_rgbBandBox->setVisible(layer->bandCount > 1);
        m_singleBandBox->setVisible(layer->bandCount == 1);
        const auto setIndex = [](QComboBox *combo, int band) {
            combo->setCurrentIndex(std::clamp(band - 1, 0, combo->count() - 1));
        };
        setIndex(m_redBandCombo, layer->redBand);
        setIndex(m_greenBandCombo, layer->greenBand);
        setIndex(m_blueBandCombo, layer->blueBand);
        setIndex(m_grayBandCombo, layer->grayBand);
        m_stretchCombo->setCurrentIndex(std::max(
            0, m_stretchCombo->findData(layer->stretchMode)));
        m_rampCombo->setCurrentText(layer->colorRamp);
        m_reverseRampCheck->setChecked(layer->colorRampReversed);
        m_noDataCheck->setChecked(layer->noDataEnabled);
        m_noDataEdit->setText(layer->noDataValue);
        m_noDataEdit->setEnabled(layer->noDataEnabled);
        const bool manualRange =
            layer->stretchMode == QStringLiteral("minmax");
        m_rasterStyleForm->setRowVisible(m_rampCombo,
                                         layer->bandCount == 1);
        m_rasterStyleForm->setRowVisible(m_reverseRampCheck,
                                         layer->bandCount == 1);
        if (auto *rgbGrid = qobject_cast<QGridLayout *>(
                m_rgbBandBox->layout())) {
            for (int row = 0; row <= 3; ++row) {
                for (int column = 2; column <= 3; ++column) {
                    if (QLayoutItem *item = rgbGrid->itemAtPosition(row, column))
                        item->widget()->setVisible(manualRange);
                }
            }
        }
        m_singleBandBox->setVisible(layer->bandCount == 1 && manualRange);
        const auto range = [layer](int band) {
            const int index = std::max(0, band - 1);
            return std::pair(layer->bandMinimums.value(index, 0.0),
                             layer->bandMaximums.value(index, 1.0));
        };
        const auto setRange = [&](int band, QDoubleSpinBox *minimum,
                                  QDoubleSpinBox *maximum) {
            const auto [min, max] = range(band);
            minimum->setValue(min);
            maximum->setValue(max);
        };
        setRange(layer->redBand, m_redMinimumSpin, m_redMaximumSpin);
        setRange(layer->greenBand, m_greenMinimumSpin, m_greenMaximumSpin);
        setRange(layer->blueBand, m_blueMinimumSpin, m_blueMaximumSpin);
        setRange(layer->grayBand, m_grayMinimumSpin, m_grayMaximumSpin);
    }
    m_refreshingLayerEditor = false;
}

void MainWindow::rebuildBandControls(int bandCount)
{
    const QList<QComboBox *> combos {m_redBandCombo, m_greenBandCombo,
                                     m_blueBandCombo, m_grayBandCombo};
    for (QComboBox *combo : combos) {
        if (combo->count() == bandCount)
            continue;
        combo->clear();
        for (int band = 1; band <= bandCount; ++band)
            combo->addItem(tr("波段 %1").arg(band), band);
    }
}

void MainWindow::updateBandRangeEditors(QComboBox *combo,
                                        QDoubleSpinBox *minimum,
                                        QDoubleSpinBox *maximum)
{
    if (m_refreshingLayerEditor)
        return;
    const LayerSnapshot *layer =
        m_controller->layerModel()->layerAt(selectedLayerRow());
    if (!layer || layer->type != QStringLiteral("raster"))
        return;
    const int index = combo->currentData().toInt() - 1;
    if (index < 0 || index >= layer->bandMinimums.size()
        || index >= layer->bandMaximums.size())
        return;
    m_refreshingLayerEditor = true;
    minimum->setValue(layer->bandMinimums.at(index));
    maximum->setValue(layer->bandMaximums.at(index));
    m_refreshingLayerEditor = false;
    applyLayerEditor();
}

void MainWindow::applyLayerEditor()
{
    const int row = selectedLayerRow();
    const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
    if (!layer)
        return;
    if (layer->type == QStringLiteral("vector")) {
        m_controller->layerModel()->setVectorStyle(
            row, layer->lineColor, layer->fillColor,
            m_lineWidthSpin->value());
        return;
    }
    const bool single = layer->bandCount == 1;
    const int red = m_redBandCombo->currentData().toInt();
    const int green = m_greenBandCombo->currentData().toInt();
    const int blue = m_blueBandCombo->currentData().toInt();
    const int gray = m_grayBandCombo->currentData().toInt();
    if (m_stretchCombo->currentData() == QStringLiteral("minmax")) {
        if (single) {
            m_controller->layerModel()->setBandRange(
                row, gray, m_grayMinimumSpin->value(),
                m_grayMaximumSpin->value());
        } else {
            m_controller->layerModel()->setBandRange(
                row, red, m_redMinimumSpin->value(), m_redMaximumSpin->value());
            m_controller->layerModel()->setBandRange(
                row, green, m_greenMinimumSpin->value(), m_greenMaximumSpin->value());
            m_controller->layerModel()->setBandRange(
                row, blue, m_blueMinimumSpin->value(), m_blueMaximumSpin->value());
        }
    }
    m_controller->layerModel()->setRasterNoData(
        row, m_noDataCheck->isChecked(),
        m_noDataEdit->text());
    m_controller->layerModel()->setRasterStyle(
        row, single ? QStringLiteral("single") : QStringLiteral("rgb"),
        single ? 1 : red, single ? 1 : green, single ? 1 : blue,
        single ? gray : 1, m_rampCombo->currentText(),
        single && m_reverseRampCheck->isChecked(),
        m_stretchCombo->currentData().toString());
}

void MainWindow::pickVectorColor(bool lineColor)
{
    const int row = selectedLayerRow();
    const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
    if (!layer || layer->type != QStringLiteral("vector"))
        return;
    const QColor color = QColorDialog::getColor(
        lineColor ? layer->lineColor : layer->fillColor, this,
        lineColor ? tr("选择线颜色") : tr("选择填充颜色"));
    if (!color.isValid())
        return;
    m_controller->layerModel()->setVectorStyle(
        row, lineColor ? color : layer->lineColor,
        lineColor ? layer->fillColor : color, m_lineWidthSpin->value());
    refreshLayerEditor();
}

void MainWindow::fitSelectedLayer()
{
    const LayerSnapshot *layer =
        m_controller->layerModel()->layerAt(selectedLayerRow());
    if (layer)
        m_canvas->fitBounds(layer->minLon, layer->minLat,
                            layer->maxLon, layer->maxLat);
}

void MainWindow::removeSelectedLayer()
{
    const int row = selectedLayerRow();
    const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
    if (!layer)
        return;
    if (QMessageBox::question(
            this, tr("移除图层"),
            tr("确定从当前项目移除“%1”吗？").arg(layer->name))
        == QMessageBox::Yes) {
        m_controller->layerModel()->removeLayer(row);
    }
}

void MainWindow::moveSelectedLayer(int offset)
{
    const int from = selectedLayerRow();
    const int to = from + offset;
    if (from < 0 || to < 0 || to >= m_controller->layerModel()->count())
        return;
    moveLayer(from, to);
}

int MainWindow::remappedIndex(int current, int from, int to)
{
    if (current == from)
        return to;
    if (from < to && current > from && current <= to)
        return current - 1;
    if (to < from && current >= to && current < from)
        return current + 1;
    return current;
}

void MainWindow::moveLayer(int from, int to)
{
    LayerModel *model = m_controller->layerModel();
    const int count = model->count();
    if (from < 0 || from >= count || to < 0 || to >= count || from == to)
        return;
    const LayerSnapshot *source = model->layerAt(from);
    const LayerSnapshot *target = model->layerAt(to);
    if (!source || !target)
        return;
    const LayerSnapshot *selected = model->layerAt(selectedLayerRow());
    const LayerSnapshot *selectedVector =
        model->layerAt(m_selectedVectorLayer);
    const QString selectedLayerId = selected ? selected->id : QString();
    const QString selectedVectorId = selectedVector
        ? selectedVector->id : QString();
    if (source->datasetId != target->datasetId) {
        model->moveDataset(source->datasetId, target->datasetId);
        m_selectedLayer = model->indexOfLayer(selectedLayerId);
        m_selectedVectorLayer = model->indexOfLayer(selectedVectorId);
        refreshLayerList();
        updateVectorLayerChoices();
        return;
    }
    m_selectedLayer = remappedIndex(m_selectedLayer, from, to);
    m_selectedVectorLayer = remappedIndex(m_selectedVectorLayer, from, to);
    model->moveLayer(from, to);
}

int MainWindow::firstVisibleVectorLayer() const
{
    for (int row = 0; row < m_controller->layerModel()->count(); ++row) {
        const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
        if (layer->visible && layer->type == QStringLiteral("vector"))
            return row;
    }
    return -1;
}

void MainWindow::updateVectorLayerChoices()
{
    if (!m_vectorLayerList)
        return;
    const int previousRow = m_selectedVectorLayer;
    const QSignalBlocker blocker(m_vectorLayerList);
    m_vectorLayerList->clear();
    for (int row = 0; row < m_controller->layerModel()->count(); ++row) {
        const LayerSnapshot *layer = m_controller->layerModel()->layerAt(row);
        if (layer->visible && layer->type == QStringLiteral("vector")) {
            auto *item = new QListWidgetItem(layerPreview(*layer), layer->name,
                                             m_vectorLayerList);
            item->setData(Qt::UserRole, row);
            item->setSizeHint(QSize(0, 36));
        }
    }
    int selection = -1;
    for (int index = 0; index < m_vectorLayerList->count(); ++index) {
        if (m_vectorLayerList->item(index)->data(Qt::UserRole).toInt()
            == previousRow) {
            selection = index;
            break;
        }
    }
    if (selection < 0 && m_vectorLayerList->count() > 0)
        selection = 0;
    m_vectorLayerList->setCurrentRow(selection);
    const QListWidgetItem *item = m_vectorLayerList->item(selection);
    m_selectedVectorLayer = item ? item->data(Qt::UserRole).toInt() : -1;
    updateVectorResult({});
}

void MainWindow::selectVectorFeature(int row, double longitude, double latitude)
{
    const double tolerance =
        360.0 / (256.0 * std::exp2(m_canvas->zoomLevel())) * 12.0;
    const QVariantMap result =
        m_controller->queryVector(row, longitude, latitude, tolerance);
    m_selectedVectorLayer = row;
    if (m_vectorLayerList) {
        for (int index = 0; index < m_vectorLayerList->count(); ++index) {
            if (m_vectorLayerList->item(index)->data(Qt::UserRole).toInt()
                == row) {
                m_vectorLayerList->setCurrentRow(index);
                break;
            }
        }
    }
    updateVectorResult(result);
    const QString wkt = result.value(QStringLiteral("geometryWkt")).toString();
    if (wkt.isEmpty())
        m_canvas->clearSelectedFeature();
    else
        m_canvas->setSelectedFeatureWkt(wkt);
}

void MainWindow::updateVectorResult(const QVariantMap &result)
{
    if (!m_vectorResults)
        return;
    m_vectorResults->clear();
    if (result.isEmpty()) {
        m_vectorHint->setText(m_selectedVectorLayer < 0
            ? tr("请选择一个矢量图层")
            : tr("在地图上单击以查询要素"));
        return;
    }
    m_vectorHint->setText(
        tr("%1 · FID %2")
            .arg(result.value(QStringLiteral("layer")).toString())
            .arg(result.value(QStringLiteral("fid")).toLongLong()));
    const QVariantList fields = result.value(QStringLiteral("fields")).toList();
    for (const QVariant &fieldValue : fields) {
        const QVariantMap field = fieldValue.toMap();
        new QTreeWidgetItem(m_vectorResults,
            {field.value(QStringLiteral("name")).toString(),
             field.value(QStringLiteral("value")).toString()});
    }
    m_vectorResults->resizeColumnToContents(0);
}

void MainWindow::updateRasterResults()
{
    if (m_activePanel != Panel::Raster || !m_rasterResults)
        return;
    const double longitude = m_canvas->mouseLongitude();
    const double latitude = m_canvas->mouseLatitude();
    if (m_hasLastRasterCoordinate
        && std::abs(longitude - m_lastRasterLongitude) < 1.0e-12
        && std::abs(latitude - m_lastRasterLatitude) < 1.0e-12)
        return;
    m_lastRasterLongitude = longitude;
    m_lastRasterLatitude = latitude;
    m_hasLastRasterCoordinate = true;
    const QVariantList results =
        m_controller->queryRasters(longitude, latitude);
    m_rasterResults->clear();
    for (const QVariant &value : results) {
        const QVariantMap raster = value.toMap();
        auto *root = new QTreeWidgetItem(
            m_rasterResults,
            {raster.value(QStringLiteral("name")).toString(),
             tr("像元 %1").arg(raster.value(QStringLiteral("pixel")).toString())});
        const QStringList bands =
            raster.value(QStringLiteral("values")).toStringList();
        for (const QString &band : bands)
            new QTreeWidgetItem(root, {band.section(u':', 0, 0),
                                       band.section(u':', 1).trimmed()});
        root->setExpanded(true);
    }
    m_rasterResults->resizeColumnToContents(0);
    if (m_canvas->coordinateMode() == QStringLiteral("pixel")) {
        m_rasterCoordinate->setText(
            QStringLiteral("X %1 · Y %2")
                .arg(m_canvas->mouseLongitude(), 0, 'f', 2)
                .arg(m_canvas->mouseLatitude(), 0, 'f', 2));
    } else {
        m_rasterCoordinate->setText(
            tr("经度 %1 · 纬度 %2")
                .arg(coordinateText(m_canvas->mouseLongitude(), true),
                     coordinateText(m_canvas->mouseLatitude(), false)));
    }
}

void MainWindow::updateCoordinates()
{
    if (m_canvas->coordinateMode() == QStringLiteral("pixel")) {
        m_coordinateLabel->setText(
            QStringLiteral("X %1   Y %2   z%3")
                .arg(m_canvas->mouseLongitude(), 0, 'f', 2)
                .arg(m_canvas->mouseLatitude(), 0, 'f', 2)
                .arg(m_canvas->zoomLevel(), 0, 'f', 1));
        return;
    }
    m_coordinateLabel->setText(
        QStringLiteral("%1   %2   z%3")
            .arg(coordinateText(m_canvas->mouseLongitude(), true),
                 coordinateText(m_canvas->mouseLatitude(), false))
            .arg(m_canvas->zoomLevel(), 0, 'f', 1));
}

QString MainWindow::coordinateText(double value, bool longitude)
{
    const QString suffix = longitude
        ? (value < 0 ? QStringLiteral("W") : QStringLiteral("E"))
        : (value < 0 ? QStringLiteral("S") : QStringLiteral("N"));
    return QStringLiteral("%1° %2").arg(std::abs(value), 0, 'f', 5).arg(suffix);
}

void MainWindow::showMetadata(int row)
{
    const QVariantMap metadata = m_controller->layerMetadata(row);
    if (metadata.isEmpty())
        return;
    auto *dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowTitle(tr("图层元数据 — %1")
        .arg(metadata.value(QStringLiteral("title")).toString()));
    dialog->resize(720, 620);
    auto *layout = new QVBoxLayout(dialog);
    auto *tree = new QTreeWidget(dialog);
    tree->setColumnCount(2);
    tree->setHeaderLabels({tr("属性"), tr("值")});
    tree->setAlternatingRowColors(true);
    QHash<QString, QTreeWidgetItem *> sections;
    const QVariantList entries = metadata.value(QStringLiteral("entries")).toList();
    for (const QVariant &entryValue : entries) {
        const QVariantMap entry = entryValue.toMap();
        const QString section = entry.value(QStringLiteral("section")).toString();
        if (!sections.contains(section)) {
            sections.insert(section,
                new QTreeWidgetItem(tree, {section, QString()}));
            sections.value(section)->setExpanded(true);
        }
        auto *item = new QTreeWidgetItem(
            sections.value(section),
            {entry.value(QStringLiteral("name")).toString(),
             entry.value(QStringLiteral("value")).toString()});
        item->setToolTip(1, item->text(1));
    }
    tree->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    tree->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    configureTableTypography(tree, tree->header());
    layout->addWidget(tree);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->show();
}

void MainWindow::showAttributeTable(int row)
{
    if (m_attributeDialog)
        m_attributeDialog->close();
    AttributeTableModel *model = m_controller->attributeTableModel();
    if (!model->loadLayer(row)) {
        QMessageBox::warning(this, tr("属性表"), model->errorMessage());
        return;
    }
    auto *dialog = new QDialog(this);
    m_attributeDialog = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowModality(Qt::NonModal);
    dialog->setWindowTitle(tr("属性表 — %1").arg(model->layerName()));
    dialog->resize(980, 650);
    auto *layout = new QVBoxLayout(dialog);
    auto *filterRow = new QHBoxLayout;
    auto *column = new QComboBox(dialog);
    column->addItems(model->columns());
    auto *filter = new QLineEdit(dialog);
    filter->setPlaceholderText(tr("输入属性值（包含匹配）"));
    auto *query = new QPushButton(tr("查询"), dialog);
    auto *clear = new QPushButton(tr("清除"), dialog);
    filterRow->addWidget(new QLabel(tr("查询列"), dialog));
    filterRow->addWidget(column);
    filterRow->addWidget(filter, 1);
    filterRow->addWidget(query);
    filterRow->addWidget(clear);
    layout->addLayout(filterRow);
    const auto applyFilter = [model, column, filter] {
        model->setFilter(column->currentIndex(), filter->text());
    };
    connect(query, &QPushButton::clicked, dialog, applyFilter);
    connect(filter, &QLineEdit::returnPressed, dialog, applyFilter);
    connect(clear, &QPushButton::clicked, dialog, [model, filter] {
        filter->clear();
        model->setFilter(-1, QString());
    });
    auto *table = new QTableView(dialog);
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSortingEnabled(false);
    table->horizontalHeader()->setSectionsClickable(true);
    table->horizontalHeader()->setStretchLastSection(true);
    configureTableTypography(table, table->horizontalHeader());
    configureTableTypography(nullptr, table->verticalHeader());
    table->resizeColumnsToContents();
    connect(table->horizontalHeader(), &QHeaderView::sectionClicked,
            dialog, [model, table, ascending = true, last = -1]
            (int section) mutable {
        if (last == section)
            ascending = !ascending;
        else
            ascending = true;
        last = section;
        model->sortByColumn(section, ascending);
        table->horizontalHeader()->setSortIndicator(
            section, ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
        table->horizontalHeader()->setSortIndicatorShown(true);
    });
    layout->addWidget(table, 1);
    auto *count = new QLabel(dialog);
    const auto updateCount = [model, count] {
        count->setText(QObject::tr("显示 %1 / %2 个要素")
                           .arg(model->filteredCount())
                           .arg(model->totalCount()));
    };
    connect(model, &AttributeTableModel::countsChanged,
            dialog, updateCount);
    updateCount();
    layout->addWidget(count);
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    buttons->button(QDialogButtonBox::Close)->setText(tr("关闭"));
    connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttons);
    dialog->show();
}

void MainWindow::applyShortcuts()
{
    for (auto it = m_shortcutActions.cbegin();
         it != m_shortcutActions.cend(); ++it) {
        it.value()->setShortcut(QKeySequence(m_controller->shortcut(it.key())));
        it.value()->setShortcutContext(Qt::ApplicationShortcut);
        if (!actions().contains(it.value()))
            addAction(it.value());
    }
    for (auto it = m_shortcutEdits.cbegin();
         it != m_shortcutEdits.cend(); ++it) {
        it.value()->setKeySequence(QKeySequence(m_controller->shortcut(it.key())));
    }
}

void MainWindow::retranslateUi()
{
    m_openAction->setText(tr("打开空间数据"));
    m_openAction->setToolTip(
        tr("打开文件 (%1)").arg(m_controller->shortcut(QStringLiteral("open"))));
    m_zoomInAction->setText(tr("放大"));
    m_zoomOutAction->setText(tr("缩小"));
    m_fitAction->setText(tr("适合所选图层范围"));
    m_panAction->setText(tr("平移"));
    m_panAction->setToolTip(tr("移动地图（启用后按住左键拖动）"));
    m_rectangleZoomAction->setText(tr("矩形框选缩放（Esc 取消）"));
    m_layersButton->setToolTip(tr("图层管理"));
    m_rasterButton->setToolTip(tr("栅格值查看"));
    m_vectorButton->setToolTip(tr("矢量属性查看"));
    m_settingsButton->setToolTip(tr("设置"));
    m_panelCloseButton->setToolTip(tr("关闭"));
    if (auto *label = m_emptyState->findChild<QLabel *>(
            QStringLiteral("emptyStateTitle")))
        label->setText(tr("打开空间数据"));
    if (auto *label = m_emptyState->findChild<QLabel *>(
            QStringLiteral("emptyStateDescription")))
        label->setText(
            tr("支持 Shapefile、GeoJSON、GeoPackage 和 GeoTIFF"));
    if (auto *button = m_emptyState->findChild<QPushButton *>(
            QStringLiteral("emptyStateOpenButton")))
        button->setText(tr("选择文件…"));
    m_rasterResults->setHeaderLabels({tr("图层 / 波段"), tr("值")});
    m_vectorResults->setHeaderLabels({tr("字段"), tr("值")});
    m_versionLabel->setText(
        tr("GeoReader %1 · Qt Widgets · Qlementine · Mapnik · GDAL")
            .arg(m_controller->version()));
    m_copyrightLabel->setText(
        tr("本APP由西北大学谭振宇团队开发。用户可以免费分发和使用；商业使用必须获得作者授权。"));
    updatePanelHeader();
    refreshLayerList();
}

void MainWindow::refreshThemedIcons()
{
    const auto icon = [](const QString &path) { return QIcon(path); };
    if (m_openAction)
        m_openAction->setIcon(icon(QStringLiteral(":/icons/ui/folder.svg")));
    if (m_zoomInAction)
        m_zoomInAction->setIcon(icon(QStringLiteral(":/icons/ui/zoom-in.svg")));
    if (m_zoomOutAction)
        m_zoomOutAction->setIcon(icon(QStringLiteral(":/icons/ui/zoom-out.svg")));
    if (m_fitAction)
        m_fitAction->setIcon(icon(QStringLiteral(":/icons/ui/fit.svg")));
    if (m_panAction)
        m_panAction->setIcon(icon(QStringLiteral(":/icons/ui/pan.svg")));
    if (m_rectangleZoomAction)
        m_rectangleZoomAction->setIcon(
            icon(QStringLiteral(":/icons/ui/rectangle.svg")));
    if (m_layersButton)
        m_layersButton->setIcon(icon(QStringLiteral(":/icons/ui/layers.svg")));
    if (m_rasterButton)
        m_rasterButton->setIcon(icon(QStringLiteral(":/icons/ui/raster.svg")));
    if (m_vectorButton)
        m_vectorButton->setIcon(icon(QStringLiteral(":/icons/ui/vector.svg")));
    if (m_settingsButton)
        m_settingsButton->setIcon(icon(QStringLiteral(":/icons/ui/settings.svg")));
    if (m_panelCloseButton)
        m_panelCloseButton->setIcon(
            icon(QStringLiteral(":/icons/ui/close.svg")));
    if (m_emptyIcon)
        m_emptyIcon->setPixmap(
            icon(QStringLiteral(":/icons/ui/folder.svg")).pixmap(25, 25));
}

void MainWindow::updatePanelHeader()
{
    if (!m_panelTitle || !m_panelSubtitle)
        return;

    m_panelTitle->setText(panelTitle(m_activePanel));
    QString subtitle;
    switch (m_activePanel) {
    case Panel::Layers:
        subtitle = m_controller->layerModel()->count() == 0
            ? tr("尚未加载空间数据")
            : tr("%1 个图层 · 点击图层编辑样式")
                  .arg(m_controller->layerModel()->count());
        break;
    case Panel::Raster:
        subtitle = tr("移动鼠标，实时读取所有可见栅格像元");
        break;
    case Panel::Vector:
        subtitle = tr("单击地图要素进行识别；命中要素会高亮");
        break;
    case Panel::Settings:
        subtitle = tr("语言、界面、Qlementine 主题与快捷键");
        break;
    case Panel::None:
        break;
    }
    m_panelSubtitle->setText(subtitle);
    m_panelSubtitle->setVisible(!subtitle.isEmpty());
}

void MainWindow::positionFloatingUi()
{
    if (!m_mapHost || !m_canvas)
        return;

    const QRect area = m_mapHost->rect();
    m_canvas->setGeometry(area);
    if (area.width() < 1 || area.height() < 1)
        return;

    constexpr int margin = 12;
    constexpr int primaryWidth = 52;
    m_toolRail->setGeometry(margin, margin, primaryWidth,
                            std::max(80, area.height() - 2 * margin));

    int preferredPanelWidth = m_activePanel == Panel::Raster ? 450
        : m_activePanel == Panel::Settings ? 410 : 360;
    if (m_activePanel == Panel::Layers) {
        const LayerSnapshot *layer =
            m_controller->layerModel()->layerAt(selectedLayerRow());
        if (layer && layer->type == QStringLiteral("raster"))
            preferredPanelWidth = 450;
    }
    const int panelWidth = std::clamp(
        preferredPanelWidth, 320, std::max(320, area.width() - 120));
    m_panelFrame->setGeometry(
        area.width() - panelWidth - margin, margin, panelWidth,
        std::max(160, area.height() - 2 * margin));

    if (m_emptyState) {
        m_emptyState->adjustSize();
        const QSize size = m_emptyState->sizeHint().expandedTo(QSize(460, 170));
        m_emptyState->setGeometry(
            (area.width() - size.width()) / 2,
            (area.height() - size.height()) / 2,
            size.width(), size.height());
    }
    if (m_coordinateFrame) {
        const int width = std::max(190, m_coordinateFrame->sizeHint().width());
        m_coordinateFrame->setGeometry(
            margin + primaryWidth + 10, area.height() - 30 - margin,
            width, 30);
    }
    if (m_statusLabel && m_statusLabel->isVisible()) {
        m_statusLabel->adjustSize();
        m_statusLabel->move((area.width() - m_statusLabel->width()) / 2,
                            margin);
    }
    m_canvas->setAttributionRightInset(
        m_activePanel == Panel::None ? 0 : panelWidth + 2 * margin);

    m_canvas->lower();
    m_toolRail->raise();
    if (m_emptyState && m_emptyState->isVisible())
        m_emptyState->raise();
    if (m_coordinateFrame)
        m_coordinateFrame->raise();
    if (m_statusLabel && m_statusLabel->isVisible())
        m_statusLabel->raise();
    if (m_panelFrame->isVisible())
        m_panelFrame->raise();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionFloatingUi();
}

void MainWindow::rebuildFloatingPanelForLanguage()
{
    const Panel panel = m_activePanel;
    m_activePanel = Panel::None;
    if (m_rasterTimer) {
        m_rasterTimer->stop();
        delete m_rasterTimer;
        m_rasterTimer = nullptr;
    }
    if (m_panelFrame) {
        delete m_panelFrame;
        m_panelFrame = nullptr;
    }
    m_shortcutEdits.clear();
    buildFloatingPanel();
    refreshLayerList();
    updateVectorLayerChoices();
    applyShortcuts();
    showPanel(panel);
    refreshThemedIcons();
    positionFloatingUi();
    m_panelRebuildScheduled = false;
}

void MainWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        const auto dialogs = findChildren<QDialog *>(
            QString(), Qt::FindDirectChildrenOnly);
        for (QDialog *dialog : dialogs)
            dialog->close();
        retranslateUi();
        if (!m_panelRebuildScheduled) {
            m_panelRebuildScheduled = true;
            QTimer::singleShot(0, this,
                               &MainWindow::rebuildFloatingPanelForLanguage);
        }
    }
}
