#pragma once
#include <QDockWidget>
#include <QVariantMap>
class AppController;
class LayerModel;
class MapCanvas;
class QComboBox;
class QFormLayout;
class QLabel;
class QCheckBox;
class QDoubleSpinBox;
class QTableWidget;
class QPlainTextEdit;
class QTimer;
class QPushButton;
class ScientificPlot;

// Native Widgets presentation. ScientificData owns no GUI state and shares the
// same read lock with the main map renderer and multidimensional importer.
class ScientificPanel final : public QDockWidget {
  Q_OBJECT
public:
  ScientificPanel(AppController *controller, MapCanvas *canvas,
                  QWidget *parent);
  QVariantMap view() const { return m_view; }
  QVariantMap curve() const { return m_curve; }
  bool busy() const { return m_jobs > 0; }
  QString layerId() const { return m_id; }
  void selectLayer(const QString &id);
  void probe(double x, double y, bool geographic);
  void changeSlice(int dimension, int index);
  void advance(int delta = 1);

private:
  void refreshChoices();
  void refreshDimensions();
  void refreshPreview();
  void refreshCurve();
  void applyDisplay();
  void updateSource(QVariantMap selection, bool clearCurve);
  QString source() const;
  void exportCsv();
  void exportPng();
  LayerModel *m_layers;
  MapCanvas *m_canvas;
  QString m_id;
  QVariantMap m_view, m_curve;
  int m_jobs = 0;
  quint64 m_generation = 0, m_probeGeneration = 0;
  bool m_refreshing = false, m_hasPoint = false, m_geographic = false;
  double m_x = 0, m_y = 0;
  QComboBox *m_layerChoice, *m_timeChoice, *m_ramp, *m_profile, *m_speed;
  QFormLayout *m_dimensions;
  QLabel *m_status, *m_legend, *m_info;
  QCheckBox *m_reverse, *m_fixed;
  QDoubleSpinBox *m_minimum, *m_maximum;
  QPushButton *m_play;
  QTimer *m_timer;
  QTableWidget *m_values;
  QPlainTextEdit *m_attributes;
  ScientificPlot *m_chart, *m_histogram;
};
