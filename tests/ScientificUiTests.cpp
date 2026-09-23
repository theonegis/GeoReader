#include "ScientificUiTests.h"
#include "AppController.h"
#include "MainWindow.h"
#include "MapCanvas.h"
#include "ScientificData.h"
#include "ScientificPanel.h"
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QPushButton>
#include <QTest>
#include <cmath>
#include <stdexcept>
namespace {
void check(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void wait(F predicate, const char *why) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 20000)
    QTest::qWait(20);
  check(predicate(), why);
}
void click(MainWindow &window, const QString &name) {
  auto button = window.findChild<QPushButton *>(name);
  check(button && button->isVisible() && button->isEnabled(),
        qPrintable("Unusable control: " + name));
  QTest::mouseClick(button, Qt::LeftButton);
  QTest::qWait(40);
}
} // namespace
int runScientificUiTests(MainWindow &window, AppController &controller) {
  try {
    controller.setLanguage("zh_CN");
    QDir().mkpath("tests/output");
    const QString base = QString::fromUtf8(GEOREADER_TEST_DATA_DIR) + "/";
    auto panel = window.findChild<ScientificPanel *>();
    check(panel, "Scientific dock missing");
    auto canvas = window.mapCanvas();
    controller.loadFiles({base + "netcdf4.nc"});
    wait(
        [&] {
          return window.findChild<QDialog *>("scientificVariableDialog") !=
                 nullptr;
        },
        "Import dialog missing");
    click(window, "scientificCancel");
    check(controller.layerModel()->count() == 0, "Cancel created a layer");
    int expected = 0;
    QString renderError;
    QObject::connect(canvas, &MapCanvas::renderError, &window,
                     [&](const QString &e) { renderError = e; });
    for (const auto &file : QStringList{"netcdf-classic.nc", "netcdf4.nc",
                                        "scientific.h5", "scientific.hdf"}) {
      controller.loadFiles({base + file});
      wait(
          [&] { return !controller.pendingMultidimensionalImport().isEmpty(); },
          "Catalog not shown");
      auto combo = window.findChild<QComboBox *>("scientificVariableBox");
      check(combo, "Variable choices missing");
      auto arrays =
          controller.pendingMultidimensionalImport().value("arrays").toList();
      int selected = -1;
      for (int i = 0; i < arrays.size(); ++i)
        if (arrays[i].toMap().value("name").toString().contains("temperature"))
          selected = i;
      check(selected >= 0, "Temperature missing");
      combo->setCurrentIndex(selected);
      QTest::qWait(50);
      window.grab().save("tests/output/variable-selection.png");
      click(window, "scientificAdd");
      ++expected;
      wait(
          [&] {
            return controller.layerModel()->count() == expected &&
                   !controller.multidimensionalImportBusy() && !panel->busy() &&
                   panel->view().contains("image");
          },
          "Imported array not displayed");
      auto layer = controller.layerModel()->layerAt(expected - 1);
      const auto id = layer->id;
      panel->selectLayer(id);
      wait([&] { return !panel->busy(); }, "Preview still loading");
      canvas->fitBounds(layer->minLon, layer->minLat, layer->maxLon,
                        layer->maxLat);
      QTest::qWait(600);
      QTest::mouseClick(canvas, Qt::LeftButton, Qt::NoModifier,
                        canvas->rect().center());
      wait(
          [&] {
            return !panel->busy() &&
                   panel->curve().value("points").toList().size() == 4;
          },
          "Map click did not draw temporal curve");
      const auto series = panel->curve();
      const auto pixel = series.value("pixel").toString().split(',');
      int x = pixel[0].trimmed().toInt(), y = pixel[1].trimmed().toInt();
      int t = 0;
      for (auto item : series.value("points").toList()) {
        auto value = item.toMap().value("value");
        if (t == 2 && x == 1 && y == 1)
          check(value.isNull(), "Missing sample connected");
        else
          check(std::abs(value.toDouble() -
                         ((100 * t + 10 * y + x) *
                              (file == "scientific.hdf" ? 1 : .5) +
                          (file == "scientific.hdf" ? 0 : 10))) < 1e-6,
                "Incorrect plotted value");
        ++t;
      }
      window.grab().save("tests/output/" + file + "-curve.png");
      const auto before = panel->view().value("timeLabel");
      click(window, "scientificNext");
      wait(
          [&] {
            return !panel->busy() && panel->view().value("timeLabel") != before;
          },
          "Next frame did not update slice");
      if (file == "netcdf4.nc") {
        QTest::qWait(1600); // Let any initial delayed statistics finish.
        const auto current = controller.layerModel()->layerAt(
            controller.layerModel()->indexOfLayer(id));
        check(current && std::abs(current->bandMinimums[0] -
              panel->view().value("displayMinimum").toDouble()) < 1e-6,
              "Initial statistics overwrote the current slice color range");
        check(current->multidimensionalSlice.contains("time=1"),
              "Layer metadata retained the old time slice");
      }
      check(panel->curve().value("points").toList().size() == 4,
            "Time stepping erased curve");
      click(window, "scientificPlay");
      QTest::qWait(1500);
      click(window, "scientificPlay");
      wait([&] { return !panel->busy() && !canvas->rendering(); },
           "Playback did not settle");
      const auto s = panel->view().value("selection").toMap();
      check(s.value("indices").toList()[s.value("time").toInt()].toInt() != 1,
            "Playback failed to advance");
      check(renderError.isEmpty(), qPrintable(renderError));
      qInfo() << "PASS native open/select/map-click/curve/slice/playback"
              << file;
    }
    auto id = controller.layerModel()->layerAt(expected - 1)->id;
    check(controller.layerModel()->moveDataset(
              controller.layerModel()->layerAt(expected - 1)->datasetId,
              controller.layerModel()->layerAt(0)->datasetId),
          "Dataset reorder failed");
    panel->selectLayer(id);
    wait([&] { return !panel->busy(); }, "Reorder timeout");
    check(panel->layerId() == id, "Reorder selected wrong variable");
    panel->probe(2, 1, false);
    controller.layerModel()->removeLayer(0);
    QTest::qWait(250);
    check(panel->curve().isEmpty(), "Deleted layer received stale curve");
    const int row = controller.layerModel()->count();
    controller.loadFiles({base + "demo.geojson"});
    wait([&] { return controller.layerModel()->count() == row + 1; },
         "Vector load failed");
    check(!controller.queryVector(row, 120.135, 30.250, .001).isEmpty(),
          "Vector identification failed");
    controller.attributeTableModel()->loadLayer(row);
    check(controller.attributeTableModel()->totalCount() == 1,
          "Vector table failed");
    qInfo() << "PASS native stable IDs, cancellation, stale-result rejection "
               "and vector regression";
    return 0;
  } catch (const std::exception &e) {
    window.grab().save("tests/output/ui-failure.png");
    qCritical() << "FAIL native UI" << e.what();
    return 1;
  }
}
