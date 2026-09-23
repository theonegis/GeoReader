#include "ScientificUiTests.h"
#include "AppController.h"
#include "MapCanvas.h"
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTest>
#include <stdexcept>

namespace {
void require(bool ok, const char *why) {
  if (!ok)
    throw std::runtime_error(why);
}
template <class F> void waitFor(F predicate, const char *why) {
  QElapsedTimer timer;
  timer.start();
  while (!predicate() && timer.elapsed() < 20000)
    QTest::qWait(30);
  require(predicate(), why);
}
void click(QObject *root, QQuickWindow *window, const QString &name) {
  auto item = root->findChild<QQuickItem *>(name);
  require(item, "UI control not found");
  require(item->isVisible() && item->isEnabled(), "UI control not usable");
  auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point.toPoint());
  QTest::qWait(60);
}
} // namespace
int runScientificUiTests(QQmlApplicationEngine &engine,
                         AppController &controller) {
  try {
    QObject *root = engine.rootObjects().first();
    auto window = qobject_cast<QQuickWindow *>(root);
    require(window, "window missing");
    QDir().mkpath("tests/output");
    QString renderError;
    auto map = root->findChild<MapCanvas *>("mapCanvas");
    QObject::connect(map, &MapCanvas::renderError, window,
                     [&](const QString &error) { renderError = error; });
    QString base = QString::fromUtf8(GEOREADER_TEST_DATA_DIR) + "/";
    controller.loadFiles({base + "netcdf-classic.nc"});
    waitFor([&] { return !controller.scientificCatalog().isEmpty(); },
            "catalog not shown");
    require(controller.layerModel()->count() == 0,
            "file opened before selection");
    window->grabWindow().save("tests/output/variable-selection.png");
    click(root, window, "scientificCancel");
    require(controller.layerModel()->count() == 0, "cancel added a layer");
    int expected = 0;
    for (const auto &file :
         QStringList{"netcdf-classic.nc", "netcdf4.nc", "scientific.h5", "scientific.hdf"}) {
      controller.loadFiles({base + file});
      waitFor([&] { return !controller.scientificCatalog().isEmpty(); },
              "variable chooser missing");
      auto vars = controller.scientificCatalog().value("variables").toList();
      int selected = -1;
      for (int i = 0; i < vars.size(); ++i)
        if (vars[i].toMap().value("name").toString().endsWith("temperature"))
          selected = i;
      require(selected >= 0, "temperature not found");
      auto combo = root->findChild<QObject *>("scientificVariableBox");
      require(combo, "variable combo missing");
      combo->setProperty("currentIndex", selected);
      QTest::qWait(100);
      click(root, window, "scientificAdd");
      ++expected;
      waitFor(
          [&] {
            return controller.layerModel()->count() == expected &&
                   !controller.scientificBusy();
          },
          "selected variable did not load");
      require(controller.scientificView().value("image").toString().startsWith(
                  "data:"),
              "preview failed");
      qInfo() << "UI view" << file << "geo"
              << controller.scientificView().value("geographic") << "active"
              << root->property("activePanel");
      window->grabWindow().save("tests/output/before-click.png");
      if (controller.scientificView().value("geographic").toBool()) {
        auto canvas = root->findChild<MapCanvas *>("mapCanvas");
        require(canvas, "map canvas missing");
        QObject::connect(
            canvas, &MapCanvas::mapClicked, window,
            [](double x, double y) { qInfo() << "UI MAP CLICK" << x << y; });
        // Put a known pixel centre to the left of the right-hand tool panel.
        canvas->fitBounds(99.5, 36.5, 104.5, 40.5);
        canvas->panBy(-160, 0);
        QTest::qWait(800);
        QTest::mouseClick(
            window, Qt::LeftButton, Qt::NoModifier,
            QPoint(int(window->width() / 2) - 160, int(window->height() / 2)));
      } else
        click(root, window, "scientificPreview");
      waitFor(
          [&] {
            return !controller.scientificBusy() &&
                   !controller.timeSeries().isEmpty();
          },
          "pixel click did not produce a series");
      const auto series = controller.timeSeries();
      require(series.value("error").toString().isEmpty(), "pixel query error");
      auto points = series.value("points").toList();
      require(points.size() == 4, "time curve is not four points");
      auto pixel = series.value("pixel").toString().split(',');
      int x = pixel[0].trimmed().toInt(), y = pixel[1].trimmed().toInt();
      const bool packed = file != "scientific.hdf";
      for (int t = 0; t < 4; ++t) {
        const auto v = points[t].toMap().value("value");
        if (t == 2 && x == 1 && y == 1)
          require(v.isNull(), "NoData connected");
        else
          require(std::abs(v.toDouble() -
                           ((100 * t + 10 * y + x) * (packed ? .5 : 1) +
                            (packed ? 10 : 0))) < 1e-6,
                  "curve value wrong");
      }
      window->grabWindow().save("tests/output/" + file + "-curve.png");
      const auto oldTime = controller.scientificView().value("timeLabel");
      click(root, window, "scientificNext");
      waitFor(
          [&] {
            return !controller.scientificBusy() &&
                   controller.scientificView().value("timeLabel") != oldTime;
          },
          "next frame did not change time");
      require(controller.timeSeries().value("points").toList().size() == 4,
              "time stepping discarded curve");
      click(root, window, "scientificPlay");
      QTest::qWait(1000);
      click(root, window, "scientificPlay");
      waitFor([&] { return !controller.scientificBusy(); },
              "animation did not settle");
      const auto selection =
          controller.scientificView().value("selection").toMap();
      require(selection.value("indices")
                      .toList()[selection.value("time").toInt()]
                      .toInt() != 1,
              "playback did not advance");
      waitFor([&] { return !map->rendering(); },
              "map rendering did not finish");
      require(renderError.isEmpty(), "map rendering reported an error");
      qInfo() << "PASS UI open/select/click/curve/slice/playback" << file;
    }
    auto id = controller.layerModel()->layerAt(expected - 1)->id;
    controller.layerModel()->moveLayer(expected - 1, 0);
    controller.inspectScientificLayer(id);
    waitFor([&] { return !controller.scientificBusy(); },
            "reorder preview timeout");
    require(controller.scientificView().value("id").toString() == id,
            "reorder changed selection identity");
    controller.requestTimeSeries(id, 2, 1, false);
    controller.layerModel()->removeLayer(0);
    QTest::qWait(300);
    require(controller.timeSeries().isEmpty(),
            "deleted layer received stale curve");
    const int vectorRow = controller.layerModel()->count();
    controller.loadFiles({base + "demo.geojson"});
    waitFor([&] { return controller.layerModel()->count() == vectorRow + 1; },
            "vector regression: GeoJSON did not load");
    root->setProperty("activePanel", "layers");
    QTest::qWait(300);
    waitFor([&] { return !map->rendering(); }, "vector render timeout");
    require(renderError.isEmpty(), "vector render error");
    require(!controller.queryVector(vectorRow, 120.135, 30.250, .001).isEmpty(),
            "vector identify failed");
    auto table = controller.attributeTableModel();
    require(table->loadLayer(vectorRow) && table->totalCount() == 1,
            "vector attribute table failed");
    table->setFilter(0, "no matching feature");
    require(table->filteredCount() == 0, "attribute filter failed");
    table->setFilter(0, "");
    require(table->filteredCount() == 1, "attribute filter reset failed");
    window->grabWindow().save("tests/output/vector-regression.png");
    qInfo() << "PASS vector loading, rendering, identification, attribute table";
    qInfo() << "PASS UI cancellation, three formats, pixel clicks, stable IDs, "
               "stale-result rejection";
    return 0;
  } catch (const std::exception &e) {
    if (auto w = qobject_cast<QQuickWindow *>(engine.rootObjects().first()))
      w->grabWindow().save("tests/output/ui-failure.png");
    qCritical() << "FAIL UI:" << e.what();
    return 1;
  }
}
