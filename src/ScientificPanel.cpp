#include "ScientificPanel.h"
#include "AppController.h"
#include "LayerModel.h"
#include "MapCanvas.h"
#include "ScientificData.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFileDialog>
#include <QFontInfo>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QHideEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPageSize>
#include <QPainter>
#include <QPainterPath>
#include <QPdfWriter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStylePainter>
#include <QStyleOptionTab>
#include <QTabBar>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include "UiTypography.h"
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

class ScientificTabBar : public QTabBar {
public:
  using QTabBar::QTabBar;
protected:
  QSize tabSizeHint(int index) const override {
    QSize size = QTabBar::tabSizeHint(index);
    QFont bold = font();
    bold.setBold(true);
    size.rwidth() += std::max(0, QFontMetrics(bold).horizontalAdvance(tabText(index)) -
                                fontMetrics().horizontalAdvance(tabText(index)));
    return size;
  }
  void paintEvent(QPaintEvent *) override {
    QStylePainter painter(this);
    const auto drawTab = [&](int index) {
      QStyleOptionTab option;
      initStyleOption(&option, index);
      QFont textFont = font();
      textFont.setBold(index == currentIndex());
      painter.setFont(textFont);
      option.fontMetrics = QFontMetrics(textFont);
      painter.drawControl(QStyle::CE_TabBarTab, option);
    };
    for (int index = 0; index < count(); ++index)
      if (index != currentIndex())
        drawTab(index);
    if (currentIndex() >= 0)
      drawTab(currentIndex());
  }
};

class ScientificTabs : public QTabWidget {
public:
  explicit ScientificTabs(QWidget *parent) : QTabWidget(parent) {
    setTabBar(new ScientificTabBar(this));
  }
};

class ScientificPlot : public QWidget {
public:
  QVariantMap data;
  bool histogram = false;
  explicit ScientificPlot(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumHeight(200);
    setMouseTracking(true);
    setCursor(Qt::CrossCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  }
  void setZoomEnabled(bool enabled) {
    m_zoomEnabled = enabled;
    m_dragging = false;
    setCursor(histogram ? Qt::ArrowCursor : Qt::CrossCursor);
    update();
  }
  void resetZoom() {
    m_zoomed = false;
    m_dragging = false;
    update();
  }
  QImage renderImage(const QSize &size, int dpi) const {
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(palette().base().color());
    const qreal scale = std::min(size.width() / qreal(std::max(1, width())),
                                 size.height() / qreal(std::max(1, height())));
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    paintContents(painter, size, std::clamp(scale, 0.25, 16.0), false);
    painter.end();
    const int dotsPerMeter = std::max(1, qRound(dpi / 0.0254));
    image.setDotsPerMeterX(dotsPerMeter);
    image.setDotsPerMeterY(dotsPerMeter);
    return image;
  }
  QRectF plotAreaFor(const QSize &size, qreal scale) const {
    const qreal left = 56 * scale;
    const qreal top = 15 * scale;
    const qreal rightInset = 18 * scale;
    const qreal bottomInset = 50 * scale;
    return QRectF(left, top,
                  std::max<qreal>(1, size.width() - left - rightInset),
                  std::max<qreal>(1, size.height() - top - bottomInset));
  }
  QRectF plotArea() const { return plotAreaFor(size(), 1); }
  QRectF visibleBounds() const {
    if (m_zoomed)
      return m_view;
    double xlo = std::numeric_limits<double>::infinity(), xhi = -xlo,
           ylo = xlo, yhi = -xlo;
    for (const auto &item : data.value("points").toList()) {
      const auto point = item.toMap();
      const double x = point.value("x").toDouble();
      xlo = std::min(xlo, x);
      xhi = std::max(xhi, x);
      if (!point.value("value").isNull()) {
        const double y = point.value("value").toDouble();
        ylo = std::min(ylo, y);
        yhi = std::max(yhi, y);
      }
    }
    if (!std::isfinite(xlo) || !std::isfinite(ylo))
      return {};
    if (xhi == xlo)
      xhi = xlo + 1;
    if (yhi == ylo) {
      ylo -= .5;
      yhi += .5;
    }
    return QRectF(xlo, ylo, xhi - xlo, yhi - ylo);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    paintContents(painter, size(), 1, true);
  }
  void mousePressEvent(QMouseEvent *event) override {
    if (!histogram && m_zoomEnabled && event->button() == Qt::LeftButton &&
        plotArea().contains(event->position()) && !visibleBounds().isEmpty()) {
      m_dragging = true;
      m_hoverVisible = false;
      m_anchor = m_current = event->position();
      update();
      event->accept();
    }
  }
  void mouseReleaseEvent(QMouseEvent *event) override {
    if (!m_dragging || event->button() != Qt::LeftButton)
      return;
    m_dragging = false;
    const auto area = plotArea();
    const auto selection = QRectF(m_anchor, event->position()).normalized().intersected(area);
    if (selection.width() >= 6 && selection.height() >= 6) {
      const auto bounds = visibleBounds();
      m_view = QRectF(
          bounds.left() + (selection.left() - area.left()) / area.width() * bounds.width(),
          bounds.top() + (area.bottom() - selection.bottom()) / area.height() * bounds.height(),
          selection.width() / area.width() * bounds.width(),
          selection.height() / area.height() * bounds.height());
      m_zoomed = true;
    }
    updateHover(event->position());
    update();
  }
  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (!histogram && m_zoomEnabled && event->button() == Qt::LeftButton)
      resetZoom();
  }
  void mouseMoveEvent(QMouseEvent *event) override {
    if (histogram)
      return;
    if (m_dragging) {
      m_current = event->position();
      update();
      return;
    }
    updateHover(event->position());
  }
  void leaveEvent(QEvent *event) override {
    m_hoverVisible = false;
    setToolTip({});
    QWidget::leaveEvent(event);
    update();
  }

private:
  QVariantMap nearestPoint(double x) const {
    QVariantMap nearest;
    double distance = std::numeric_limits<double>::infinity();
    for (const auto &item : data.value("points").toList()) {
      const auto point = item.toMap();
      const double d = std::abs(point.value("x").toDouble() - x);
      if (d < distance) {
        nearest = point;
        distance = d;
      }
    }
    return nearest;
  }
  void updateHover(const QPointF &position) {
    const auto bounds = visibleBounds();
    const bool inside = plotArea().contains(position) && !bounds.isEmpty() &&
                        !data.value("points").toList().isEmpty();
    m_hoverPos = position;
    m_hoverVisible = inside;
    if (!inside) {
      setToolTip({});
      update();
      return;
    }
    const double x = bounds.left() + (position.x() - plotArea().left()) /
                                          plotArea().width() * bounds.width();
    const auto nearest = nearestPoint(x);
    const QString value = nearest.value("value").isNull()
                              ? QStringLiteral("缺测")
                              : QString::number(nearest.value("value").toDouble(), 'g', 10);
    const QString unit = data.value("unit").toString();
    setToolTip(nearest.value("label").toString() + "\n" + value +
               (unit.isEmpty() ? QString() : " " + unit));
    update();
  }
  void paintContents(QPainter &p, const QSize &canvas, qreal scale,
                     bool interactive) const {
    QFont font = this->font();
    int pixels = QFontInfo(font).pixelSize();
    if (pixels <= 0)
      pixels = 13;
    font.setPixelSize(std::max(1, int(std::lround(pixels * scale))));
    p.setFont(font);
    p.fillRect(QRect(QPoint(0, 0), canvas), palette().base());
    p.setPen(palette().text().color());
    const QRectF area = plotAreaFor(canvas, scale);
    if (histogram) {
      const auto bins = data.value("histogram").toList();
      double high = 1;
      for (auto value : bins)
        high = std::max(high, value.toDouble());
      p.setBrush(QColor("#4387d8"));
      p.setPen(Qt::NoPen);
      for (int i = 0; i < bins.size(); ++i) {
        const double h = area.height() * bins[i].toDouble() / high;
        p.drawRect(QRectF(area.left() + i * area.width() / bins.size(),
                          area.bottom() - h, area.width() / bins.size() - scale,
                          h));
      }
      p.setPen(palette().text().color());
      p.drawText(QRectF(10 * scale, canvas.height() - 42 * scale,
                        canvas.width() - 20 * scale, 40 * scale),
                 Qt::AlignCenter,
                 QString("预览抽样：%1 / %2 有效，均值 %3")
                     .arg(data.value("validCount").toInt())
                     .arg(data.value("sampleCount").toInt())
                     .arg(data.value("mean").toDouble(), 0, 'g', 7));
      return;
    }
    const auto points = data.value("points").toList();
    if (points.isEmpty()) {
      p.drawText(QRect(QPoint(0, 0), canvas), Qt::AlignCenter,
                 QStringLiteral("在地图或像素视图上单击查看曲线"));
      return;
    }
    const QRectF bounds = visibleBounds();
    if (bounds.isEmpty()) {
      p.drawText(QRect(QPoint(0, 0), canvas), Qt::AlignCenter,
                 QStringLiteral("所选像元均为缺测值"));
      return;
    }
    const double xlo = bounds.left(), xhi = bounds.right(),
                 ylo = bounds.top(), yhi = bounds.bottom();
    const auto toPixel = [&](double x, double y) {
      return QPointF(area.left() + (x - xlo) / (xhi - xlo) * area.width(),
                     area.bottom() - (y - ylo) / (yhi - ylo) * area.height());
    };
    for (int i = 0; i < 5; ++i) {
      const double y = area.top() + i * area.height() / 4;
      p.setPen(palette().mid().color());
      p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
      p.setPen(palette().text().color());
      p.drawText(QRectF(0, y - 9 * scale, 51 * scale, 18 * scale),
                 Qt::AlignRight | Qt::AlignVCenter,
                 QString::number(yhi - i * (yhi - ylo) / 4, 'g', 5));
    }
    p.setPen(QPen(QColor("#3478f6"), std::max(1.0, 2 * scale)));
    p.save();
    p.setClipRect(area);
    QPainterPath path;
    bool connected = false;
    int visibleCount = 0;
    for (const auto &item : points) {
      const double x = item.toMap().value("x").toDouble();
      if (x >= xlo && x <= xhi)
        ++visibleCount;
    }
    for (auto item : points) {
      auto v = item.toMap();
      if (v.value("value").isNull()) {
        connected = false;
        continue;
      }
      const QPointF point = toPixel(v.value("x").toDouble(), v.value("value").toDouble());
      if (connected)
        path.lineTo(point);
      else
        path.moveTo(point);
      connected = true;
      if (visibleCount <= 200)
        p.drawEllipse(point, 2.5 * scale, 2.5 * scale);
    }
    p.drawPath(path);
    p.restore();
    p.setPen(palette().text().color());
    const auto labelAt = [this](double x) { return nearestPoint(x).value("label").toString(); };
    p.drawText(QRectF(area.left(), area.bottom() + 7 * scale, area.width(), 18 * scale),
               Qt::AlignLeft, labelAt(xlo));
    p.drawText(QRectF(area.left(), area.bottom() + 25 * scale, area.width(), 18 * scale),
               Qt::AlignRight, labelAt(xhi));
    if (interactive && m_dragging) {
      p.setPen(QPen(QColor("#3478f6"), 1, Qt::DashLine));
      p.setBrush(QColor(52, 120, 246, 40));
      p.drawRect(QRectF(m_anchor, m_current).normalized().intersected(area));
    }
    if (!interactive || !m_hoverVisible || m_dragging)
      return;
    const double hoverX = xlo + (m_hoverPos.x() - area.left()) / area.width() * (xhi - xlo);
    const auto nearest = nearestPoint(hoverX);
    if (nearest.isEmpty())
      return;
    const QPointF sample = toPixel(nearest.value("x").toDouble(),
                                   nearest.value("value").isNull()
                                       ? ylo
                                       : nearest.value("value").toDouble());
    if (sample.x() >= area.left() && sample.x() <= area.right()) {
      p.setPen(QPen(QColor(52, 120, 246, 110), std::max(1.0, scale)));
      p.drawLine(QPointF(sample.x(), area.top()), QPointF(sample.x(), area.bottom()));
      if (!nearest.value("value").isNull()) {
        p.setPen(QPen(Qt::white, std::max(1.0, 2 * scale)));
        p.setBrush(QColor("#3478f6"));
        p.drawEllipse(sample, 4 * scale, 4 * scale);
      }
    }
    const QString value = nearest.value("value").isNull()
                              ? QStringLiteral("缺测")
                              : QString::number(nearest.value("value").toDouble(), 'g', 10);
    const QString unit = data.value("unit").toString();
    const QString text = nearest.value("label").toString() + "\n" + value +
                         (unit.isEmpty() ? QString() : " " + unit);
    const QRect textBounds = p.fontMetrics().boundingRect(
        QRect(0, 0, 280, 160), Qt::AlignLeft | Qt::TextDontClip, text);
    const QSize box(textBounds.width() + 16, textBounds.height() + 12);
    QPoint origin = m_hoverPos.toPoint() + QPoint(16, 16);
    if (origin.x() + box.width() > canvas.width() - 4)
      origin.setX(std::max(4, int(m_hoverPos.x()) - box.width() - 16));
    if (origin.y() + box.height() > canvas.height() - 4)
      origin.setY(std::max(4, int(m_hoverPos.y()) - box.height() - 16));
    const QRect frame(origin, box);
    p.setPen(QPen(palette().mid().color(), 1));
    p.setBrush(palette().toolTipBase());
    p.drawRoundedRect(frame, 6, 6);
    p.setPen(palette().toolTipText().color());
    p.drawText(frame.adjusted(8, 6, -8, -6), Qt::AlignLeft | Qt::AlignVCenter, text);
  }
  bool m_zoomed = false, m_dragging = false, m_zoomEnabled = false;
  bool m_hoverVisible = false;
  QRectF m_view;
  QPointF m_anchor, m_current, m_hoverPos;
};

struct PngExportChoice {
  bool accepted = false;
  int width = 0;
  int height = 0;
  int dpi = 150;
};

PngExportChoice askImageExportChoice(QWidget *parent, const QSize &suggested,
                                     const QString &title, const QString &objectName) {
  QDialog dialog(parent);
  dialog.setObjectName(objectName);
  dialog.setWindowFlag(Qt::Sheet, false);
  dialog.setWindowModality(Qt::ApplicationModal);
  dialog.setWindowTitle(title);
  auto layout = new QVBoxLayout(&dialog);
  auto form = new QFormLayout;
  auto width = new QSpinBox(&dialog);
  width->setObjectName(QStringLiteral("scientificPngWidth"));
  width->setRange(64, 16000);
  width->setValue(std::clamp(suggested.width(), 64, 16000));
  width->setSuffix(ScientificPanel::tr(" 像素"));
  auto height = new QSpinBox(&dialog);
  height->setObjectName(QStringLiteral("scientificPngHeight"));
  height->setRange(64, 16000);
  height->setValue(std::clamp(suggested.height(), 64, 16000));
  height->setSuffix(ScientificPanel::tr(" 像素"));
  auto dpi = new QSpinBox(&dialog);
  dpi->setObjectName(QStringLiteral("scientificPngDpi"));
  dpi->setRange(36, 2400);
  dpi->setValue(150);
  dpi->setSuffix(ScientificPanel::tr(" DPI"));
  form->addRow(ScientificPanel::tr("宽度"), width);
  form->addRow(ScientificPanel::tr("高度"), height);
  form->addRow(QStringLiteral("DPI"), dpi);
  auto hint = new QLabel(&dialog);
  hint->setWordWrap(true);
  const auto refresh = [&] {
    const double widthCm = width->value() / double(dpi->value()) * 2.54;
    const double heightCm = height->value() / double(dpi->value()) * 2.54;
    hint->setText(ScientificPanel::tr("打印尺寸约 %1 × %2 cm")
                      .arg(widthCm, 0, 'f', 1)
                      .arg(heightCm, 0, 'f', 1));
  };
  refresh();
  QObject::connect(width, &QSpinBox::valueChanged, &dialog, refresh);
  QObject::connect(height, &QSpinBox::valueChanged, &dialog, refresh);
  QObject::connect(dpi, &QSpinBox::valueChanged, &dialog, refresh);
  layout->addLayout(form);
  layout->addWidget(hint);
  auto buttons = new QDialogButtonBox(
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
  QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  layout->addWidget(buttons);
  PngExportChoice choice;
  if (dialog.exec() != QDialog::Accepted)
    return choice;
  choice.accepted = true;
  choice.width = width->value();
  choice.height = height->value();
  choice.dpi = dpi->value();
  return choice;
}

void saveCurveCsv(QWidget *parent, QLabel *status, const QVariantMap &curve) {
  if (curve.value("points").toList().isEmpty()) {
    status->setText(ScientificPanel::tr("请先点击一个像元"));
    return;
  }
  const auto path = QFileDialog::getSaveFileName(
      parent, ScientificPanel::tr("导出曲线"), QStringLiteral("series.csv"),
      QStringLiteral("CSV (*.csv)"));
  if (path.isEmpty())
    return;
  QSaveFile file(path);
  const auto bytes = ScientificData::csv(curve);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    status->setText(ScientificPanel::tr("CSV 保存失败"));
}

void saveCurvePng(QWidget *parent, QLabel *status, ScientificPlot *plot) {
  if (plot->data.value("points").toList().isEmpty()) {
    status->setText(ScientificPanel::tr("请先点击一个像元"));
    return;
  }
  const auto choice = askImageExportChoice(
      parent, plot->size(), ScientificPanel::tr("导出 PNG"),
      QStringLiteral("scientificPngOptions"));
  if (!choice.accepted)
    return;
  const auto path = QFileDialog::getSaveFileName(
      parent, ScientificPanel::tr("导出曲线视图"), QStringLiteral("curve.png"),
      QStringLiteral("PNG (*.png)"));
  if (path.isEmpty())
    return;
  const QImage image =
      plot->renderImage(QSize(choice.width, choice.height), choice.dpi);
  if (!image.save(path, "PNG"))
    status->setText(ScientificPanel::tr("PNG 保存失败"));
}

void saveCurvePdf(QWidget *parent, QLabel *status, ScientificPlot *plot) {
  if (plot->data.value("points").toList().isEmpty()) {
    status->setText(ScientificPanel::tr("请先点击一个像元"));
    return;
  }
  const auto choice = askImageExportChoice(
      parent, plot->size(), ScientificPanel::tr("导出 PDF"),
      QStringLiteral("scientificPdfOptions"));
  if (!choice.accepted)
    return;
  const auto path = QFileDialog::getSaveFileName(
      parent, ScientificPanel::tr("导出曲线视图"), QStringLiteral("curve.pdf"),
      QStringLiteral("PDF (*.pdf)"));
  if (path.isEmpty())
    return;
  QPdfWriter writer(path);
  writer.setResolution(choice.dpi);
  writer.setTitle(ScientificPanel::tr("曲线"));
  writer.setPageSize(QPageSize(
      QSizeF(choice.width * 25.4 / choice.dpi, choice.height * 25.4 / choice.dpi),
      QPageSize::Millimeter));
  writer.setPageMargins(QMarginsF(0, 0, 0, 0));
  QPainter painter(&writer);
  const QImage image =
      plot->renderImage(QSize(choice.width, choice.height), choice.dpi);
  if (!painter.isActive()) {
    status->setText(ScientificPanel::tr("PDF 保存失败"));
    return;
  }
  painter.drawImage(QRectF(0, 0, painter.device()->width(), painter.device()->height()),
                    image);
}

class ScientificCurveDialog : public QDialog {
public:
  ScientificCurveDialog(const QVariantMap &curve, const QString &title,
                        QWidget *parent)
      : QDialog(parent) {
    setObjectName(QStringLiteral("scientificCurveDialog"));
    // Application-modal, not a macOS sheet. A sheet keeps the parent title bar
    // and disables its close button, so this window would have no way to close.
    setWindowFlags(Qt::Dialog | Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                   Qt::WindowCloseButtonHint | Qt::WindowMinMaxButtonsHint);
    setWindowFlag(Qt::Sheet, false);
    setWindowModality(Qt::ApplicationModal);
    setWindowTitle(title);
    setSizeGripEnabled(true);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);
    m_plot = new ScientificPlot(this);
    m_plot->setObjectName(QStringLiteral("scientificDialogPlot"));
    m_plot->data = curve;
    m_plot->setMinimumHeight(280);
    layout->addWidget(m_plot, 1);
    auto row = new QHBoxLayout;
    row->setSpacing(6);
    auto zoom = new QPushButton(tr("放大"), this);
    zoom->setObjectName(QStringLiteral("scientificDialogZoom"));
    zoom->setCheckable(true);
    zoom->setToolTip(tr("启用后在曲线上拖框放大；双击恢复全图。"));
    auto reset = new QPushButton(tr("恢复"), this);
    reset->setObjectName(QStringLiteral("scientificDialogReset"));
    reset->setToolTip(tr("恢复完整曲线，并回到悬停读数"));
    auto csv = new QPushButton(tr("导出 CSV"), this);
    csv->setObjectName(QStringLiteral("scientificDialogExportCsv"));
    auto png = new QPushButton(tr("导出 PNG"), this);
    png->setObjectName(QStringLiteral("scientificDialogExportPng"));
    png->setToolTip(tr("按指定宽高和 DPI 导出当前曲线视图，保留放大范围"));
    auto pdf = new QPushButton(tr("导出 PDF"), this);
    pdf->setObjectName(QStringLiteral("scientificDialogExportPdf"));
    pdf->setToolTip(tr("按指定宽高和 DPI 导出当前曲线视图为 PDF，保留放大范围"));
    for (auto button : {zoom, reset, csv, png, pdf})
      row->addWidget(button, 1);
    layout->addLayout(row);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    connect(zoom, &QPushButton::toggled, m_plot, &ScientificPlot::setZoomEnabled);
    connect(reset, &QPushButton::clicked, this, [this, zoom] {
      m_plot->resetZoom();
      zoom->setChecked(false);
    });
    connect(csv, &QPushButton::clicked, this,
            [this] { saveCurveCsv(this, m_status, m_plot->data); });
    connect(png, &QPushButton::clicked, this,
            [this] { saveCurvePng(this, m_status, m_plot); });
    connect(pdf, &QPushButton::clicked, this,
            [this] { saveCurvePdf(this, m_status, m_plot); });
  }

private:
  ScientificPlot *m_plot = nullptr;
  QLabel *m_status = nullptr;
};

static QPixmap imageFromData(const QVariant &uri) {
  auto text = uri.toString();
  QPixmap image;
  image.loadFromData(
      QByteArray::fromBase64(text.mid(text.indexOf(',') + 1).toLatin1()));
  return image;
}
ScientificPanel::ScientificPanel(AppController *controller, MapCanvas *canvas,
                                 QWidget *parent)
    : QWidget(parent), m_layers(controller->layerModel()),
      m_canvas(canvas) {
  setObjectName("scientificPanel");
  auto outerLayout = new QVBoxLayout(this);
  outerLayout->setContentsMargins(12, 12, 12, 12);
  auto scroll = new QScrollArea(this);
  scroll->setFrameShape(QFrame::NoFrame);
  scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  scroll->setAutoFillBackground(false);
  scroll->viewport()->setAutoFillBackground(false);
  scroll->setWidgetResizable(true);
  auto body = new QWidget(scroll);
  auto layout = new QVBoxLayout(body);
  m_layerChoice = new QComboBox(body);
  m_layerChoice->setObjectName("scientificLayerChoice");
  layout->addWidget(m_layerChoice);
  auto hint = new QLabel(
      tr("单击地图像元绘制曲线；切换时间时其他维度保持固定。"), body);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  m_timeChoice = new QComboBox(body);
  m_timeChoice->setObjectName("scientificTimeChoice");
  auto timeForm = new QFormLayout;
  timeForm->addRow(tr("时间维度"), m_timeChoice);
  layout->addLayout(timeForm);
  m_dimensions = new QFormLayout;
  layout->addLayout(m_dimensions);
  auto playback = new QHBoxLayout;
  auto previous = new QPushButton("◀", body);
  auto next = new QPushButton("▶", body);
  next->setObjectName("scientificNext");
  m_play = new QPushButton(tr("播放"), body);
  m_play->setObjectName("scientificPlay");
  m_speed = new QComboBox(body);
  for (double speed : {.5, 1., 2., 4.})
    m_speed->addItem(QString::number(speed) + "×", speed);
  m_speed->setCurrentIndex(1);
  for (auto widget : QList<QWidget *>{previous, m_play, next, m_speed})
    playback->addWidget(widget);
  layout->addLayout(playback);
  auto display = new QHBoxLayout;
  m_ramp = new QComboBox(body);
  m_ramp->addItems({"Viridis", "Plasma", "Inferno", "Magma", "Cividis", "Turbo",
                    "Terrain", "Gray"});
  m_reverse = new QCheckBox(tr("反转"), body);
  m_fixed = new QCheckBox(tr("固定范围"), body);
  display->addWidget(m_ramp, 1);
  display->addWidget(m_reverse);
  display->addWidget(m_fixed);
  layout->addLayout(display);
  auto range = new QHBoxLayout;
  m_minimum = new QDoubleSpinBox(body);
  m_maximum = new QDoubleSpinBox(body);
  for (auto spin : {m_minimum, m_maximum}) {
    spin->setRange(-1e20, 1e20);
    spin->setDecimals(6);
    range->addWidget(spin);
  }
  m_maximum->setValue(1);
  m_minimum->setEnabled(false);
  m_maximum->setEnabled(false);
  layout->addLayout(range);
  m_legend = new QLabel(body);
  m_legend->setScaledContents(true);
  m_legend->setFixedHeight(12);
  layout->addWidget(m_legend);
  m_info = new QLabel(body);
  m_info->setWordWrap(true);
  layout->addWidget(m_info);
  auto tabs = new ScientificTabs(body);
  tabs->setObjectName("scientificResultTabs");
  auto curves = new QWidget(tabs);
  auto curvesLayout = new QVBoxLayout(curves);
  m_profile = new QComboBox(curves);
  m_profile->addItem(tr("时间曲线"), "time");
  m_profile->addItem(tr("行剖面（X）"), "row");
  m_profile->addItem(tr("列剖面（Y）"), "column");
  curvesLayout->addWidget(m_profile);
  m_chart = new ScientificPlot(curves);
  m_chart->setObjectName("scientificCurvePlot");
  curvesLayout->addWidget(m_chart);
  auto zoomRow = new QHBoxLayout;
  zoomRow->setSpacing(6);
  auto enlarge = new QPushButton(tr("放大窗口"), curves);
  enlarge->setObjectName("scientificCurveWindow");
  enlarge->setToolTip(tr("在更大的窗口中查看曲线，可拖动窗口边缘调整大小"));
  auto csv = new QPushButton(tr("导出 CSV"), curves);
  csv->setObjectName("scientificExportCsv");
  auto png = new QPushButton(tr("导出 PNG"), curves);
  png->setObjectName("scientificExportPng");
  png->setToolTip(tr("选择宽高和 DPI 后导出当前曲线"));
  for (auto button : {enlarge, csv, png})
    zoomRow->addWidget(button, 1);
  curvesLayout->addLayout(zoomRow);
  connect(enlarge, &QPushButton::clicked, this, &ScientificPanel::showCurveWindow);
  tabs->addTab(curves, tr("曲线"));
  auto valuesPage = new QWidget(tabs);
  auto valuesLayout = new QVBoxLayout(valuesPage);
  m_valuesMode = new QComboBox(valuesPage);
  m_valuesMode->setObjectName("scientificValuesMode");
  m_valuesMode->addItem(tr("曲线采样值"), "series");
  m_valuesMode->addItem(tr("当前切片的 5×5 邻域"), "neighborhood");
  valuesLayout->addWidget(m_valuesMode);
  m_valuesInfo = new QLabel(valuesPage);
  m_valuesInfo->setWordWrap(true);
  valuesLayout->addWidget(m_valuesInfo);
  m_values = new QTableWidget(valuesPage);
  m_values->setObjectName("scientificValues");
  m_values->setFont(UiTypography::dataFont());
  m_values->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_values->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  valuesLayout->addWidget(m_values);
  tabs->addTab(valuesPage, tr("数值"));
  connect(m_valuesMode, &QComboBox::currentIndexChanged, this,
          [this] {
            refreshValues();
            if (m_valuesMode->currentData().toString() == "neighborhood")
              refreshCurve(false);
          });
  connect(controller, &AppController::fontChanged, m_values, [this] {
    m_values->setFont(UiTypography::dataFont());
    m_values->resizeRowsToContents();
  });
  refreshValues();
  m_histogram = new ScientificPlot(tabs);
  m_histogram->histogram = true;
  tabs->addTab(m_histogram, tr("分布"));
  m_attributes = new QPlainTextEdit(tabs);
  m_attributes->setReadOnly(true);
  tabs->addTab(m_attributes, tr("属性"));
  layout->addWidget(tabs);
  m_status = new QLabel(body);
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  layout->addStretch();
  scroll->setWidget(body);
  body->setAutoFillBackground(false);
  outerLayout->addWidget(scroll);
  m_timer = new QTimer(this);
  m_timer->setInterval(500);
  connect(m_timer, &QTimer::timeout, this, [this] {
    if (!busy() && !m_canvas->rendering())
      advance();
  });
  connect(m_play, &QPushButton::clicked, this, [this] {
    if (m_timer->isActive())
      m_timer->stop();
    else
      m_timer->start();
    m_play->setText(m_timer->isActive() ? tr("暂停") : tr("播放"));
  });
  connect(m_speed, &QComboBox::currentIndexChanged, this, [this] {
    m_timer->setInterval(int(500 / m_speed->currentData().toDouble()));
  });
  connect(previous, &QPushButton::clicked, this, [this] { advance(-1); });
  connect(next, &QPushButton::clicked, this, [this] { advance(); });
  connect(m_layerChoice, &QComboBox::currentIndexChanged, this, [this] {
    if (!m_refreshing)
      selectLayer(m_layerChoice->currentData().toString());
  });
  connect(m_timeChoice, &QComboBox::currentIndexChanged, this, [this] {
    if (m_refreshing)
      return;
    auto s = ScientificData::decode(source());
    s["time"] = m_timeChoice->currentData();
    updateSource(s, true);
  });
  connect(m_profile, &QComboBox::currentIndexChanged, this,
          [this] { refreshCurve(); });
  connect(m_ramp, &QComboBox::currentIndexChanged, this,
          &ScientificPanel::applyDisplay);
  connect(m_reverse, &QCheckBox::toggled, this, &ScientificPanel::applyDisplay);
  connect(m_fixed, &QCheckBox::toggled, this, &ScientificPanel::applyDisplay);
  connect(m_minimum, &QDoubleSpinBox::editingFinished, this,
          &ScientificPanel::applyDisplay);
  connect(m_maximum, &QDoubleSpinBox::editingFinished, this,
          &ScientificPanel::applyDisplay);
  connect(csv, &QPushButton::clicked, this, &ScientificPanel::exportCsv);
  connect(png, &QPushButton::clicked, this, &ScientificPanel::exportPng);
  connect(m_canvas, &MapCanvas::mapClicked, this, [this](double x, double y) {
    if (isVisible() && !m_id.isEmpty())
      probe(x, y, m_canvas->coordinateMode() != "pixel");
  });
  connect(m_layers, &QAbstractItemModel::rowsInserted, this,
          [this](const QModelIndex &, int first, int last) {
            refreshChoices();
            for (int row = first; row <= last; ++row) {
              auto layer = m_layers->layerAt(row);
              if (layer &&
                  !ScientificData::decode(layer->sourceUri).isEmpty()) {
                const auto id = layer->id;
                QTimer::singleShot(0, this, [this, id] { selectLayer(id); });
              }
            }
          });
  connect(m_layers, &QAbstractItemModel::rowsRemoved, this, [this] {
    refreshChoices();
    if (m_layers->indexOfLayer(m_id) < 0) {
      ++m_generation;
      ++m_probeGeneration;
      m_id.clear();
      m_view.clear();
      m_curve.clear();
      m_hasPoint = false;
      m_timer->stop();
      m_chart->data.clear();
      m_chart->resetZoom();
      refreshValues();
      m_chart->update();
      emit dismissalRequested();
    }
  });
  connect(m_layers, &QAbstractItemModel::rowsMoved, this,
          &ScientificPanel::refreshChoices);
  connect(m_layers, &QAbstractItemModel::modelReset, this,
          &ScientificPanel::refreshChoices);
}

void ScientificPanel::hideEvent(QHideEvent *event) {
  m_timer->stop();
  m_play->setText(tr("播放"));
  QWidget::hideEvent(event);
}
QString ScientificPanel::source() const {
  auto layer = m_layers->layerAt(m_layers->indexOfLayer(m_id));
  return layer ? layer->sourceUri : QString();
}
void ScientificPanel::refreshChoices() {
  QSignalBlocker blocker(m_layerChoice);
  m_layerChoice->clear();
  for (const auto &layer : m_layers->snapshots())
    if (!ScientificData::decode(layer.sourceUri).isEmpty())
      m_layerChoice->addItem(layer.name, layer.id);
  m_layerChoice->setCurrentIndex(m_layerChoice->findData(m_id));
}
void ScientificPanel::selectLayer(const QString &id) {
  if (id.isEmpty() || m_layers->indexOfLayer(id) < 0)
    return;
  const bool changed = m_id != id;
  m_id = id;
  if (ScientificData::decode(source()).isEmpty())
    return;
  ++m_generation;
  ++m_probeGeneration;
  if (changed) {
    m_curve.clear();
    m_hasPoint = false;
    m_chart->data.clear();
    m_chart->resetZoom();
    refreshValues();
    m_chart->update();
  }
  refreshChoices();
  refreshDimensions();
  refreshPreview();
  auto layer = m_layers->layerAt(m_layers->indexOfLayer(id));
  if (layer) {
    m_canvas->setCoordinateMode(layer->coordinateMode, layer->pixelWidth,
                                layer->pixelHeight);
    m_canvas->fitBounds(layer->minLon, layer->minLat, layer->maxLon,
                        layer->maxLat);
  }
  emit activationRequested();
}
void ScientificPanel::refreshDimensions() {
  m_refreshing = true;
  while (m_dimensions->count()) {
    auto item = m_dimensions->takeAt(0);
    delete item->widget();
    delete item;
  }
  m_timeChoice->clear();
  m_timeChoice->addItem(tr("无时间轴"), -1);
  const auto s = ScientificData::decode(source());
  const auto dims = s.value("dimensions").toList(),
             indices = s.value("indices").toList();
  for (int i = 0; i < dims.size(); ++i) {
    if (i == s.value("x").toInt() || i == s.value("y").toInt())
      continue;
    auto dim = dims[i].toMap();
    m_timeChoice->addItem(dim.value("name").toString(), i);
    auto spin = new QSpinBox(this);
    spin->setObjectName("scientificSlice" + QString::number(i));
    spin->setRange(0, int(std::min<qulonglong>(
                          dim.value("size").toULongLong() - 1, INT_MAX)));
    spin->setValue(indices.value(i).toInt());
    m_dimensions->addRow(dim.value("name").toString() + " / " +
                             dim.value("size").toString(),
                         spin);
    connect(spin, &QSpinBox::valueChanged, this, [this, i](int value) {
      if (!m_refreshing)
        changeSlice(i, value);
    });
  }
  m_timeChoice->setCurrentIndex(m_timeChoice->findData(s.value("time")));
  m_play->setEnabled(s.value("time").toInt() >= 0);
  const auto display = s.value("display").toMap();
  m_ramp->setCurrentText(display.value("ramp", "Viridis").toString());
  m_reverse->setChecked(display.value("reversed").toBool());
  m_fixed->setChecked(display.value("fixed").toBool());
  if (m_fixed->isChecked()) {
    m_minimum->setValue(display.value("minimum").toDouble());
    m_maximum->setValue(display.value("maximum").toDouble());
  }
  QStringList text{s.value("file").toString(), s.value("array").toString(),
                   s.value("dataType").toString(), s.value("unit").toString()};
  for (auto attr : s.value("attributes").toList())
    text << attr.toMap().value("name").toString() + " = " +
                attr.toMap().value("value").toString();
  m_attributes->setPlainText(text.join('\n'));
  m_refreshing = false;
}
void ScientificPanel::refreshPreview() {
  const auto uri = source();
  if (uri.isEmpty())
    return;
  const auto generation = m_generation;
  const auto id = m_id;
  ++m_jobs;
  m_status->setText(tr("正在读取切片…"));
  auto watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
          [this, watcher, generation, id, uri] {
            const auto result = watcher->result();
            watcher->deleteLater();
            --m_jobs;
            if (generation != m_generation || id != m_id || uri != source())
              return;
            m_view = result;
            if (m_valuesMode->currentData().toString() == "neighborhood")
              refreshValues();
            m_view["selection"] = ScientificData::decode(uri);
            m_status->setText(result.value("error").toString());
            if (!m_fixed->isChecked()) {
              m_minimum->setValue(result.value("displayMinimum").toDouble());
              m_maximum->setValue(result.value("displayMaximum").toDouble());
            }
            m_minimum->setEnabled(m_fixed->isChecked());
            m_maximum->setEnabled(m_fixed->isChecked());
            m_legend->setPixmap(imageFromData(result.value("legend")));
            m_histogram->data = result;
            m_histogram->update();
            m_info->setText(
                QString("%1\n色标：%2 ～ %3 %4")
                    .arg(result.value("timeLabel").toString())
                    .arg(result.value("displayMinimum").toDouble(), 0, 'g', 7)
                    .arg(result.value("displayMaximum").toDouble(), 0, 'g', 7)
                    .arg(ScientificData::decode(uri).value("unit").toString()));
            if (result.contains("image")) {
              m_layers->setBandRanges(
                  id, {result.value("displayMinimum").toDouble()},
                  {result.value("displayMaximum").toDouble()});
              auto layer = m_layers->layerAt(m_layers->indexOfLayer(id));
              if (layer)
                m_layers->setRasterStyle(m_layers->indexOfLayer(id), "single",
                                         1, 1, 1, 1, m_ramp->currentText(),
                                         m_reverse->isChecked(), "minmax");
            }
          });
  watcher->setFuture(
      QtConcurrent::run([uri] { return ScientificData::preview(uri); }));
}
void ScientificPanel::updateSource(QVariantMap s, bool clearCurve) {
  if (m_id.isEmpty())
    return;
  ++m_generation;
  if (clearCurve)
    ++m_probeGeneration;
  if (clearCurve) {
    m_curve.clear();
    m_chart->data.clear();
    m_chart->resetZoom();
    refreshValues();
    m_chart->update();
  }
  QStringList slices;
  const auto dims = s.value("dimensions").toList(),
             indices = s.value("indices").toList();
  for (int i = 0; i < dims.size(); ++i)
    if (i != s.value("x").toInt() && i != s.value("y").toInt())
      slices << dims[i].toMap().value("name").toString() + "=" +
                    indices.value(i).toString();
  m_layers->setScientificSource(m_id, ScientificData::encode(s),
                                slices.join(", "));
  refreshChoices();
  refreshPreview();
  if (clearCurve && m_hasPoint)
    refreshCurve();
}
void ScientificPanel::changeSlice(int dimension, int index) {
  auto s = ScientificData::decode(source());
  auto indices = s.value("indices").toList();
  auto dims = s.value("dimensions").toList();
  if (dimension < 0 || dimension >= indices.size() ||
      dimension == s.value("x").toInt() || dimension == s.value("y").toInt() ||
      index < 0 ||
      qulonglong(index) >= dims[dimension].toMap().value("size").toULongLong())
    return;
  indices[dimension] = index;
  s["indices"] = indices;
  const bool clearCurve = dimension != s.value("time").toInt() ||
                         m_profile->currentData().toString() != "time";
  updateSource(s, clearCurve);
  if (!clearCurve && m_hasPoint &&
      m_valuesMode->currentData().toString() == "neighborhood")
    refreshCurve(false);
}
void ScientificPanel::advance(int delta) {
  auto s = ScientificData::decode(source());
  int dimension = s.value("time", -1).toInt();
  auto dims = s.value("dimensions").toList();
  if (dimension < 0 || dimension >= dims.size())
    return;
  const auto size = dims[dimension].toMap().value("size").toLongLong();
  if (size <= 0)
    return;
  int index =
      int((s.value("indices").toList()[dimension].toLongLong() + delta + size) %
          size);
  changeSlice(dimension, index);
  auto spin =
      findChild<QSpinBox *>("scientificSlice" + QString::number(dimension));
  if (spin) {
    QSignalBlocker block(spin);
    spin->setValue(index);
  }
}
void ScientificPanel::applyDisplay() {
  if (m_refreshing)
    return;
  if (m_fixed->isChecked() && m_minimum->value() >= m_maximum->value()) {
    m_status->setText(tr("最小值必须小于最大值"));
    return;
  }
  auto s = ScientificData::decode(source());
  if (s.isEmpty())
    return;
  s["display"] = QVariantMap{{"ramp", m_ramp->currentText()},
                             {"reversed", m_reverse->isChecked()},
                             {"fixed", m_fixed->isChecked()},
                             {"minimum", m_minimum->value()},
                             {"maximum", m_maximum->value()}};
  updateSource(s, false);
}
void ScientificPanel::probe(double x, double y, bool geographic) {
  m_x = x;
  m_y = y;
  m_geographic = geographic;
  m_hasPoint = true;
  refreshCurve();
  if (geographic)
    m_canvas->setSelectedFeatureWkt(
        QString("POINT (%1 %2)").arg(x, 0, 'g', 17).arg(y, 0, 'g', 17));
}
void ScientificPanel::refreshCurve(bool resetZoom) {
  if (!m_hasPoint || source().isEmpty())
    return;
  const auto uri = source(), id = m_id,
             profile = m_profile->currentData().toString();
  const auto generation = ++m_probeGeneration;
  const double x = m_x, y = m_y;
  const bool geo = m_geographic;
  ++m_jobs;
  auto watcher = new QFutureWatcher<QVariantMap>(this);
  connect(watcher, &QFutureWatcher<QVariantMap>::finished, this,
          [this, watcher, id, generation, resetZoom] {
            auto result = watcher->result();
            watcher->deleteLater();
            --m_jobs;
            if (id != m_id || generation != m_probeGeneration ||
                m_layers->indexOfLayer(id) < 0)
              return;
            m_curve = result;
            m_chart->data = result;
            if (resetZoom)
              m_chart->resetZoom();
            m_chart->update();
            m_status->setText(
                result.contains("error")
                    ? result.value("error").toString()
                    : QString("像元（列, 行）：%1 · %2 · %3")
                          .arg(result.value("pixel").toString(),
                               result.value("unit").toString(),
                               result.value("calendar").toString()));
            refreshValues();
          });
  watcher->setFuture(QtConcurrent::run([uri, x, y, geo, profile] {
    return ScientificData::series(uri, x, y, geo, profile);
  }));
}
void ScientificPanel::refreshValues() {
  const bool neighborhood = m_valuesMode->currentData().toString() == "neighborhood";
  const QString profile = m_curve.value("profile", "time").toString();
  const QString unit = m_curve.value("unit").toString();
  const QString valueTitle = unit.isEmpty() ? tr("实际值") : tr("实际值 (%1)").arg(unit);
  m_values->clear();
  m_values->setColumnCount(5);
  m_values->setHorizontalHeaderLabels({neighborhood ? tr("切片时间") :
      (profile == "time" ? tr("时间 / 坐标") : tr("剖面坐标")),
      tr("列"), tr("行"), valueTitle, tr("原始值")});
  m_values->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
  const auto samples = m_curve.value(neighborhood ? "cells" : "points").toList();
  m_values->setRowCount(samples.size());
  QString explanation = neighborhood
      ? tr("当前时间切片中，点击位置周围最多 5×5 个像元；边缘处不足 25 个。")
      : (profile == "time"
          ? tr("同一像元随时间的变化：行列固定，其他非时间维度保持不变。")
          : tr("当前切片的空间剖面：沿所选行或列取样。"));
  if (!neighborhood && samples.isEmpty() && !m_curve.isEmpty() && !m_curve.contains("error"))
    explanation = tr("此变量没有时间维度，请选择行/列剖面，或查看当前切片的 5×5 邻域。");
  explanation += tr(" 行列号从 0 开始。");
  explanation += tr(" 实际值 = 文件原始值 × %1 + %2。")
        .arg(m_curve.value("scale", 1).toDouble(), 0, 'g', 10)
        .arg(m_curve.value("offset", 0).toDouble(), 0, 'g', 10);
  m_valuesInfo->setText(explanation);
  const QString sliceTime = m_view.value("timeLabel").toString();
  for (int row = 0; row < samples.size(); ++row) {
    const auto sample = samples[row].toMap();
    const QString fullCoordinate = neighborhood ? sliceTime : sample.value("label").toString();
    QString coordinateLabel = fullCoordinate;
    if (coordinateLabel.endsWith("T00:00:00Z"))
      coordinateLabel.chop(10);
    const QStringList texts = {
      coordinateLabel,
      sample.value("column").toString(), sample.value("row").toString(),
      sample.value("value").isNull() ? tr("缺测") :
          QString::number(sample.value("value").toDouble(), 'g', 10),
      sample.value("raw").isNull() ? tr("缺测") :
          QString::number(sample.value("raw").toDouble(), 'g', 10)};
    for (int column = 0; column < texts.size(); ++column) {
      auto item = new QTableWidgetItem(texts[column]);
      if (column == 0)
        item->setToolTip(fullCoordinate);
      const auto key = column == 3 ? "value" : "raw";
      if (column >= 3 && !sample.value(key).isNull())
        item->setToolTip(QString::number(sample.value(key).toDouble(), 'g', 17));
      m_values->setItem(row, column, item);
    }
  }
}
void ScientificPanel::showCurveWindow() {
  if (m_curve.value("points").toList().isEmpty()) {
    m_status->setText(tr("请先点击一个像元"));
    return;
  }
  ScientificCurveDialog dialog(m_curve, m_profile->currentText(), this);
  const QWidget *host = window();
  const QSize desired(int(std::lround(host->width() * 0.8)),
                      int(std::lround(host->height() * 0.8)));
  dialog.setMinimumSize(640, 420);
  dialog.resize(desired);
  const QRect hostFrame = host->frameGeometry();
  dialog.move(hostFrame.center() - QPoint(desired.width() / 2, desired.height() / 2));
  dialog.exec();
}
void ScientificPanel::exportCsv() { saveCurveCsv(this, m_status, m_curve); }
void ScientificPanel::exportPng() { saveCurvePng(this, m_status, m_chart); }
