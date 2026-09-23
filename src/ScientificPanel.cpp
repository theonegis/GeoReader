#include "ScientificPanel.h"
#include "AppController.h"
#include "LayerModel.h"
#include "MapCanvas.h"
#include "ScientificData.h"
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QFutureWatcher>
#include <QHeaderView>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QtConcurrent/QtConcurrentRun>
#include <algorithm>
#include <climits>
#include <cmath>
#include <limits>

class ScientificPlot : public QWidget {
public:
  QVariantMap data;
  bool histogram = false;
  explicit ScientificPlot(QWidget *parent = nullptr) : QWidget(parent) {
    setMinimumHeight(200);
    setMouseTracking(true);
  }
  void paintEvent(QPaintEvent *) override {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), palette().base());
    p.setPen(palette().text().color());
    const QRectF area(56, 15, width() - 74, height() - 65);
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
                          area.bottom() - h, area.width() / bins.size() - 1,
                          h));
      }
      p.setPen(palette().text().color());
      p.drawText(QRectF(10, height() - 42, width() - 20, 40), Qt::AlignCenter,
                 QString("预览抽样：%1 / %2 有效，均值 %3")
                     .arg(data.value("validCount").toInt())
                     .arg(data.value("sampleCount").toInt())
                     .arg(data.value("mean").toDouble(), 0, 'g', 7));
      return;
    }
    const auto points = data.value("points").toList();
    if (points.isEmpty()) {
      p.drawText(rect(), Qt::AlignCenter,
                 QStringLiteral("在地图或像素视图上单击查看曲线"));
      return;
    }
    double xlo = std::numeric_limits<double>::infinity(), xhi = -xlo, ylo = xlo,
           yhi = -xlo;
    for (auto item : points) {
      auto v = item.toMap();
      const double x = v.value("x").toDouble();
      xlo = std::min(xlo, x);
      xhi = std::max(xhi, x);
      if (!v.value("value").isNull()) {
        const double y = v.value("value").toDouble();
        ylo = std::min(ylo, y);
        yhi = std::max(yhi, y);
      }
    }
    if (!std::isfinite(ylo)) {
      p.drawText(rect(), Qt::AlignCenter, "所选像元均为缺测值");
      return;
    }
    if (xhi == xlo)
      xhi = xlo + 1;
    if (yhi == ylo) {
      ylo -= .5;
      yhi += .5;
    }
    for (int i = 0; i < 5; ++i) {
      const double y = area.top() + i * area.height() / 4;
      p.setPen(palette().mid().color());
      p.drawLine(QPointF(area.left(), y), QPointF(area.right(), y));
      p.setPen(palette().text().color());
      p.drawText(QRectF(0, y - 9, 51, 18), Qt::AlignRight | Qt::AlignVCenter,
                 QString::number(yhi - i * (yhi - ylo) / 4, 'g', 5));
    }
    p.setPen(QPen(QColor("#3478f6"), 2));
    QPainterPath path;
    bool connected = false;
    for (auto item : points) {
      auto v = item.toMap();
      if (v.value("value").isNull()) {
        connected = false;
        continue;
      }
      QPointF point(area.left() + (v.value("x").toDouble() - xlo) /
                                      (xhi - xlo) * area.width(),
                    area.bottom() - (v.value("value").toDouble() - ylo) /
                                        (yhi - ylo) * area.height());
      if (connected)
        path.lineTo(point);
      else
        path.moveTo(point);
      connected = true;
      p.drawEllipse(point, 2.5, 2.5);
    }
    p.drawPath(path);
    p.setPen(palette().text().color());
    p.drawText(QRectF(area.left(), area.bottom() + 7, area.width(), 18),
               Qt::AlignLeft, points.first().toMap().value("label").toString());
    p.drawText(QRectF(area.left(), area.bottom() + 25, area.width(), 18),
               Qt::AlignRight, points.last().toMap().value("label").toString());
  }
  void mouseMoveEvent(QMouseEvent *event) override {
    const auto points = data.value("points").toList();
    if (histogram || points.isEmpty())
      return;
    double lo = std::numeric_limits<double>::infinity(), hi = -lo;
    for (auto v : points) {
      lo = std::min(lo, v.toMap().value("x").toDouble());
      hi = std::max(hi, v.toMap().value("x").toDouble());
    }
    const double x = lo + (event->position().x() - 56) /
                              std::max(1, width() - 74) * (hi - lo);
    QVariantMap nearest;
    double distance = std::numeric_limits<double>::infinity();
    for (auto v : points) {
      auto point = v.toMap();
      double d = std::abs(point.value("x").toDouble() - x);
      if (d < distance) {
        nearest = point;
        distance = d;
      }
    }
    setToolTip(
        nearest.value("label").toString() + "\n" +
        (nearest.value("value").isNull()
             ? QString("缺测")
             : QString::number(nearest.value("value").toDouble(), 'g', 10)) +
        " " + data.value("unit").toString());
  }
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
    : QDockWidget(tr("变量与时序"), parent), m_layers(controller->layerModel()),
      m_canvas(canvas) {
  setObjectName("scientificPanel");
  setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  setMinimumWidth(380);
  auto scroll = new QScrollArea(this);
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
  auto tabs = new QTabWidget(body);
  auto curves = new QWidget(tabs);
  auto curvesLayout = new QVBoxLayout(curves);
  m_profile = new QComboBox(curves);
  m_profile->addItem(tr("时间曲线"), "time");
  m_profile->addItem(tr("行剖面（X）"), "row");
  m_profile->addItem(tr("列剖面（Y）"), "column");
  curvesLayout->addWidget(m_profile);
  m_chart = new ScientificPlot(curves);
  curvesLayout->addWidget(m_chart);
  auto csv = new QPushButton(tr("导出 CSV"), curves);
  curvesLayout->addWidget(csv);
  tabs->addTab(curves, tr("曲线"));
  m_values = new QTableWidget(tabs);
  m_values->setColumnCount(4);
  m_values->setHorizontalHeaderLabels(
      {tr("列"), tr("行"), tr("原始值"), tr("物理值")});
  m_values->setEditTriggers(QAbstractItemView::NoEditTriggers);
  m_values->horizontalHeader()->setSectionResizeMode(
      QHeaderView::ResizeToContents);
  tabs->addTab(m_values, tr("数值"));
  m_histogram = new ScientificPlot(tabs);
  m_histogram->histogram = true;
  tabs->addTab(m_histogram, tr("分布"));
  m_attributes = new QPlainTextEdit(tabs);
  m_attributes->setReadOnly(true);
  tabs->addTab(m_attributes, tr("属性"));
  layout->addWidget(tabs);
  auto png = new QPushButton(tr("导出预览 PNG（最长边 800 像素）"), body);
  layout->addWidget(png);
  m_status = new QLabel(body);
  m_status->setWordWrap(true);
  layout->addWidget(m_status);
  layout->addStretch();
  scroll->setWidget(body);
  setWidget(scroll);
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
          &ScientificPanel::refreshCurve);
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
      m_chart->update();
      hide();
    }
  });
  connect(m_layers, &QAbstractItemModel::rowsMoved, this,
          &ScientificPanel::refreshChoices);
  connect(m_layers, &QAbstractItemModel::modelReset, this,
          &ScientificPanel::refreshChoices);
  connect(this, &QDockWidget::visibilityChanged, this, [this](bool visible) {
    if (!visible) {
      m_timer->stop();
      m_play->setText(tr("播放"));
    }
  });
  hide();
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
  show();
  raise();
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
  updateSource(s, dimension != s.value("time").toInt() ||
                      m_profile->currentData().toString() != "time");
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
void ScientificPanel::refreshCurve() {
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
          [this, watcher, id, generation] {
            auto result = watcher->result();
            watcher->deleteLater();
            --m_jobs;
            if (id != m_id || generation != m_probeGeneration ||
                m_layers->indexOfLayer(id) < 0)
              return;
            m_curve = result;
            m_chart->data = result;
            m_chart->update();
            m_status->setText(
                result.contains("error")
                    ? result.value("error").toString()
                    : QString("像元（列, 行）：%1 · %2 · %3")
                          .arg(result.value("pixel").toString(),
                               result.value("unit").toString(),
                               result.value("calendar").toString()));
            const auto cells = result.value("cells").toList();
            m_values->setRowCount(cells.size());
            int row = 0;
            for (auto item : cells) {
              auto cell = item.toMap();
              int column = 0;
              for (auto key : {"column", "row", "raw", "value"}) {
                auto value = cell.value(key);
                m_values->setItem(row, column++,
                                  new QTableWidgetItem(value.isNull()
                                                           ? QString("—")
                                                           : value.toString()));
              }
              ++row;
            }
          });
  watcher->setFuture(QtConcurrent::run([uri, x, y, geo, profile] {
    return ScientificData::series(uri, x, y, geo, profile);
  }));
}
void ScientificPanel::exportCsv() {
  if (m_curve.value("points").toList().isEmpty()) {
    m_status->setText(tr("请先点击一个像元"));
    return;
  }
  auto path = QFileDialog::getSaveFileName(this, tr("导出曲线"), "series.csv",
                                           "CSV (*.csv)");
  if (path.isEmpty())
    return;
  QSaveFile file(path);
  const auto bytes = ScientificData::csv(m_curve);
  if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() ||
      !file.commit())
    m_status->setText(tr("CSV 保存失败"));
}
void ScientificPanel::exportPng() {
  if (!m_view.contains("image"))
    return;
  auto path = QFileDialog::getSaveFileName(this, tr("导出预览"), "preview.png",
                                           "PNG (*.png)");
  if (path.isEmpty())
    return;
  if (!imageFromData(m_view.value("image")).save(path, "PNG"))
    m_status->setText(tr("PNG 保存失败"));
}
