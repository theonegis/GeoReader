#include "ScientificUiTests.h"
#include "AppController.h"
#include "MainWindow.h"
#include "MapCanvas.h"
#include "ScientificData.h"
#include "ScientificPanel.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDir>
#include <QEvent>
#include <QElapsedTimer>
#include <QPushButton>
#include <QListWidget>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QDragLeaveEvent>
#include <QMessageBox>
#include <QTimer>
#include <QToolButton>
#include <QScrollArea>
#include <QTreeWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QCheckBox>
#include <QTreeWidgetItemIterator>
#include <QTextDocument>
#include <QTemporaryDir>
#include <QLineEdit>
#include <QMouseEvent>
#include <QSignalSpy>
#include <QSpinBox>
#include <gdal_priv.h>
#include <QTest>
#include <cmath>
#include <stdexcept>
#include <memory>
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
    check(panel, "Scientific controls missing");
    auto floatingPanel = window.findChild<QWidget *>("floatingPanel");
    check(floatingPanel && floatingPanel->isAncestorOf(panel),
          "Scientific controls do not share the standard floating panel");
    auto canvas = window.mapCanvas();
    controller.loadFiles({base + "netcdf4.nc"});
    wait(
        [&] {
          return window.findChild<QDialog *>("scientificVariableDialog") !=
                 nullptr;
        },
        "Import dialog missing");
    auto initialVariables =
        window.findChild<QTreeWidget *>("scientificVariableTree");
    check(initialVariables && initialVariables->topLevelItemCount() >= 2,
          "Scientific import does not expose a multi-variable checklist");
    check(initialVariables->topLevelItem(0)->flags() & Qt::ItemIsUserCheckable,
          "Scientific variables cannot be selected independently");
    click(window, "scientificCancel");
    check(controller.layerModel()->count() == 0, "Cancel created a layer");
    int expected = 0;
    QSignalSpy imports(&controller, &AppController::layerAdded);
    int automaticResets = 0;
    QTimer importResponses;
    QObject::connect(&importResponses, &QTimer::timeout, &window, [&] {
      auto box = window.findChild<QMessageBox *>("coordinateContextConfirmation");
      if (!box)
        return;
      for (auto button : box->buttons())
        if (box->buttonRole(button) == QMessageBox::AcceptRole) {
          ++automaticResets;
          button->click();
          return;
        }
    });
    importResponses.start(10);
    QString renderError;
    QObject::connect(canvas, &MapCanvas::renderError, &window,
                     [&](const QString &e) { renderError = e; });
    for (const auto &file : QStringList{"netcdf-classic.nc", "netcdf4.nc",
                                        "scientific.h5", "scientific.hdf"}) {
      controller.loadFiles({base + file});
      wait(
          [&] { return !controller.pendingMultidimensionalImport().isEmpty(); },
          "Catalog not shown");
      auto variables = window.findChild<QTreeWidget *>("scientificVariableTree");
      check(variables, "Variable choices missing");
      auto arrays =
          controller.pendingMultidimensionalImport().value("arrays").toList();
      int selected = -1;
      for (int i = 0; i < arrays.size(); ++i)
        if (arrays[i].toMap().value("name").toString().contains("temperature"))
          selected = i;
      check(selected >= 0, "Temperature missing");
      for (int row = 0; row < variables->topLevelItemCount(); ++row)
        variables->topLevelItem(row)->setCheckState(0, Qt::Unchecked);
      variables->topLevelItem(selected)->setCheckState(0, Qt::Checked);
      variables->setCurrentItem(variables->topLevelItem(selected));
      QTest::qWait(50);
      // These synthetic HDF fixtures have coordinate values but no CRS.
      // Assign the known fixture CRS explicitly for geographic overlay tests.
      auto sourceCrs = window.findChild<QLineEdit *>("scientificSourceCrs");
      check(sourceCrs, "Source CRS editor missing");
      sourceCrs->setText("EPSG:4326");
      window.grab().save("tests/output/variable-selection.png");
      const int previousImports = imports.count();
      click(window, "scientificAdd");
      wait(
          [&] {
            return imports.count() == previousImports + 1 &&
                   !controller.multidimensionalImportBusy() && !panel->busy() &&
                   panel->view().contains("image");
          },
          "Imported array not displayed");
      expected = controller.layerModel()->count();
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
      if (file == "netcdf4.nc") {
        auto tabs = panel->findChild<QTabWidget *>("scientificResultTabs");
        auto values = panel->findChild<QTableWidget *>("scientificValues");
        auto mode = panel->findChild<QComboBox *>("scientificValuesMode");
        auto plot = panel->findChild<QWidget *>("scientificCurvePlot");
        auto enlarge = panel->findChild<QPushButton *>("scientificCurveWindow");
        auto csv = panel->findChild<QPushButton *>("scientificExportCsv");
        auto png = panel->findChild<QPushButton *>("scientificExportPng");
        check(tabs && values && mode && plot && enlarge && csv && png,
              "Curve inspection controls missing");
        check(panel->findChild<QPushButton *>("scientificZoom") == nullptr &&
                  panel->findChild<QPushButton *>("scientificResetZoom") == nullptr,
              "Embedded curve still offers rectangle zoom");
        tabs->setCurrentIndex(1);
        QTest::qWait(40);
        check(values->rowCount() == 4 && !values->isColumnHidden(3) &&
                  !values->isColumnHidden(4),
              "Values tab must show actual and stored time sample values together");
        for (int row = 0; row < values->rowCount(); ++row)
          check(values->item(row, 1)->text().toInt() == x &&
                    values->item(row, 2)->text().toInt() == y,
                "Values tab changed the sampled pixel across time");
        check(std::abs(values->item(0, 3)->text().toDouble() -
                       (values->item(0, 4)->text().toDouble() * .5 + 10)) < 1e-6,
              "Actual and raw columns do not reflect the file calibration");
        mode->setCurrentIndex(1);
        wait([&] { return !panel->busy(); }, "Neighborhood read did not settle");
        check(values->rowCount() > 4,
              "Explicit neighborhood mode did not show surrounding pixels");
        const double originalNeighbor = values->item(0, 3)->text().toDouble();
        const auto sourceLayer = controller.layerModel()->layerAt(
            controller.layerModel()->indexOfLayer(id));
        const int timeDimension = ScientificData::decode(sourceLayer->sourceUri)
                                      .value("time").toInt();
        panel->changeSlice(timeDimension, 1);
        wait([&] { return !panel->busy(); }, "Updated neighborhood did not settle");
        check(std::abs(values->item(0, 3)->text().toDouble() - originalNeighbor - 50) < 1e-6,
              "Neighborhood retained the previous time slice");
        panel->changeSlice(timeDimension, 0);
        wait([&] { return !panel->busy(); }, "Restored neighborhood did not settle");
        mode->setCurrentIndex(0);
        QTest::qWait(40);
        check(values->visualItemRect(values->item(0, 3)).right() < values->viewport()->width() &&
                  values->visualItemRect(values->item(0, 4)).right() < values->viewport()->width(),
              "Actual and raw columns do not fit together in the values viewport");
        window.grab().save("tests/output/scientific-time-values.png");
        tabs->setCurrentIndex(0);
        QTest::qWait(40);
        check(enlarge->geometry().y() == csv->geometry().y() &&
                  csv->geometry().y() == png->geometry().y() &&
                  enlarge->geometry().right() < csv->geometry().left() &&
                  csv->geometry().right() < png->geometry().left(),
              "Curve actions are not arranged in a single non-overlapping row");
        const auto embedded = plot->grab().toImage();
        const QPoint from(56 + int((plot->width() - 74) * .4), 30),
                     to(56 + int((plot->width() - 74) * .8), plot->height() - 90);
        QTest::mousePress(plot, Qt::LeftButton, Qt::NoModifier, from);
        QTest::mouseMove(plot, to);
        QTest::mouseRelease(plot, Qt::LeftButton, Qt::NoModifier, to);
        QEvent leaveEmbedded(QEvent::Leave);
        QApplication::sendEvent(plot, &leaveEmbedded);
        check(plot->grab().toImage() == embedded,
              "Embedded curve accepted rectangle zoom");
        const char *dialogError = nullptr;
        bool dialogSeen = false;
        QTimer dialogProbe;
        dialogProbe.setInterval(20);
        QObject::connect(&dialogProbe, &QTimer::timeout, &window, [&] {
          auto dialog = panel->findChild<QDialog *>("scientificCurveDialog");
          if (!dialog || !dialog->isVisible() || dialogSeen)
            return;
          dialogProbe.stop();
          dialogSeen = true;
          QTest::qWait(40);
          const QSize desired(int(std::lround(window.width() * 0.8)),
                              int(std::lround(window.height() * 0.8)));
          const QPoint expected = window.frameGeometry().center() -
                                  QPoint(desired.width() / 2, desired.height() / 2);
          auto dialogPlot = dialog->findChild<QWidget *>("scientificDialogPlot");
          auto zoom = dialog->findChild<QPushButton *>("scientificDialogZoom");
          auto reset = dialog->findChild<QPushButton *>("scientificDialogReset");
          auto dialogCsv = dialog->findChild<QPushButton *>("scientificDialogExportCsv");
          auto dialogPng = dialog->findChild<QPushButton *>("scientificDialogExportPng");
          auto dialogPdf = dialog->findChild<QPushButton *>("scientificDialogExportPdf");
          if (!dialogPlot || !zoom || !reset || !dialogCsv || !dialogPng || !dialogPdf)
            dialogError = "Curve window controls missing";
          else if (!dialog->isModal() ||
                   !(dialog->windowFlags() & Qt::WindowTitleHint) ||
                   !(dialog->windowFlags() & Qt::WindowCloseButtonHint))
            dialogError = "Curve window is not a closable system-titled modal dialog";
          else if (dialog->maximumWidth() <= dialog->width())
            dialogError = "Curve window cannot be resized";
          else if (std::abs(dialog->width() - desired.width()) > 4 ||
                   std::abs(dialog->height() - desired.height()) > 4)
            dialogError = "Curve window is not 80 percent of the main window";
          else if (std::abs(dialog->pos().x() - expected.x()) > 20 ||
                   std::abs(dialog->pos().y() - expected.y()) > 20)
            dialogError = "Curve window is not centered on the main window";
          else if (zoom->isChecked())
            dialogError = "Curve window starts in rectangle-zoom mode";
          else if (!(zoom->geometry().y() == reset->geometry().y() &&
                     reset->geometry().y() == dialogCsv->geometry().y() &&
                     dialogCsv->geometry().y() == dialogPng->geometry().y() &&
                     dialogPng->geometry().y() == dialogPdf->geometry().y() &&
                     zoom->geometry().right() < reset->geometry().left() &&
                     reset->geometry().right() < dialogCsv->geometry().left() &&
                     dialogCsv->geometry().right() < dialogPng->geometry().left() &&
                     dialogPng->geometry().right() < dialogPdf->geometry().left()))
            dialogError = "Curve window actions are not in a single row";
          else {
            const QPoint dialogFrom(56 + int((dialogPlot->width() - 74) * .4), 30);
            const QPoint dialogTo(56 + int((dialogPlot->width() - 74) * .8),
                                  dialogPlot->height() - 90);
            const auto beforeHover = dialogPlot->grab().toImage();
            const QPoint hoverAt(70, 40);
            QMouseEvent hoverMove(QEvent::MouseMove, QPointF(hoverAt),
                                  dialogPlot->mapToGlobal(hoverAt), Qt::NoButton,
                                  Qt::NoButton, Qt::NoModifier);
            QApplication::sendEvent(dialogPlot, &hoverMove);
            const QString hoverText = dialogPlot->toolTip();
            const auto withHover = dialogPlot->grab().toImage();
            QEvent leavePlot(QEvent::Leave);
            QApplication::sendEvent(dialogPlot, &leavePlot);
            const auto full = dialogPlot->grab().toImage();
            if (hoverText.isEmpty())
              dialogError = "Hover label text missing";
            else if (withHover == beforeHover)
              dialogError = "Hover label was not drawn";
            else if (full != beforeHover)
              dialogError = "Hover label stayed after the pointer left";
            else {
                QTest::mousePress(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogFrom);
                QTest::mouseMove(dialogPlot, dialogTo);
                QTest::mouseRelease(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogTo);
                QApplication::sendEvent(dialogPlot, &leavePlot);
                if (dialogPlot->grab().toImage() != full)
                  dialogError = "Default curve window accepted rectangle zoom";
                else {
                  zoom->click();
                  QTest::mousePress(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogFrom);
                  QTest::mouseMove(dialogPlot, dialogTo);
                  QTest::mouseRelease(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogTo);
                  if (dialogPlot->grab().toImage() == full)
                    dialogError = "Dragging did not magnify the curve";
                  else {
                    const QPoint zoomHover(58, 50);
                    QMouseEvent zoomMove(QEvent::MouseMove, QPointF(zoomHover),
                                         dialogPlot->mapToGlobal(zoomHover), Qt::NoButton,
                                         Qt::NoButton, Qt::NoModifier);
                    QApplication::sendEvent(dialogPlot, &zoomMove);
                    if (!dialogPlot->toolTip().startsWith(
                            series.value("points").toList()[2].toMap().value("label").toString()))
                      dialogError = "Zoomed tooltip used the full-series coordinate range";
                    else {
                      window.grab().save("tests/output/scientific-curve-zoom.png");
                      reset->click();
                      QApplication::sendEvent(dialogPlot, &leavePlot);
                      if (zoom->isChecked() || dialogPlot->grab().toImage() != full)
                        dialogError = "Reset did not restore the full curve and hover mode";
                      else {
                        zoom->click();
                        QTest::mousePress(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogFrom);
                        QTest::mouseRelease(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogTo);
                        QTest::mouseDClick(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogFrom);
                        QApplication::sendEvent(dialogPlot, &leavePlot);
                        if (!zoom->isChecked() || dialogPlot->grab().toImage() != full)
                          dialogError = "Double click did not reset curve zoom";
                        else {
                          zoom->click();
                          QTest::mousePress(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogFrom);
                          QTest::mouseRelease(dialogPlot, Qt::LeftButton, Qt::NoModifier, dialogTo);
                          QApplication::sendEvent(dialogPlot, &leavePlot);
                          if (dialogPlot->grab().toImage() != full)
                            dialogError = "Disabled zoom still changed the curve range";
                          else {
                            bool sawPngOptions = false;
                            QTimer pngProbe;
                            pngProbe.setInterval(20);
                            QObject::connect(&pngProbe, &QTimer::timeout, &window, [&] {
                              auto options = panel->findChild<QDialog *>("scientificPngOptions");
                              if (!options || !options->isVisible())
                                return;
                              pngProbe.stop();
                              auto width = options->findChild<QSpinBox *>("scientificPngWidth");
                              auto height = options->findChild<QSpinBox *>("scientificPngHeight");
                              auto dpi = options->findChild<QSpinBox *>("scientificPngDpi");
                              sawPngOptions = width && height && dpi && width->value() >= 64 &&
                                              height->value() >= 64 && dpi->value() >= 36;
                              if (!sawPngOptions)
                                dialogError = "PNG export does not offer width, height and DPI";
                              options->reject();
                            });
                            pngProbe.start();
                            dialogPng->click();
                            pngProbe.stop();
                            if (!dialogError && !sawPngOptions)
                              dialogError = "PNG export options did not open";
                            else if (!dialogError) {
                              bool sawPdfOptions = false;
                              QTimer pdfProbe;
                              pdfProbe.setInterval(20);
                              QObject::connect(&pdfProbe, &QTimer::timeout, &window, [&] {
                                auto options = panel->findChild<QDialog *>("scientificPdfOptions");
                                if (!options || !options->isVisible())
                                  return;
                                pdfProbe.stop();
                                auto width = options->findChild<QSpinBox *>("scientificPngWidth");
                                auto height = options->findChild<QSpinBox *>("scientificPngHeight");
                                auto dpi = options->findChild<QSpinBox *>("scientificPngDpi");
                                sawPdfOptions = width && height && dpi && width->value() >= 64 &&
                                                height->value() >= 64 && dpi->value() >= 36 &&
                                                (options->windowFlags() & Qt::WindowCloseButtonHint);
                                if (!sawPdfOptions)
                                  dialogError = "PDF export does not offer width, height and DPI";
                                options->reject();
                              });
                              pdfProbe.start();
                              dialogPdf->click();
                              pdfProbe.stop();
                              if (!dialogError && !sawPdfOptions)
                                dialogError = "PDF export options did not open";
                            }
                          }
                        }
                      }
                    }
                  }
                }
            }
          }
          if (dialog->isVisible())
            dialog->close();
        });
        dialogProbe.start();
        enlarge->click();
        dialogProbe.stop();
        check(dialogSeen, "Curve window did not open");
        check(dialogError == nullptr, dialogError ? dialogError : "Curve window failed");
        check(panel->findChild<QDialog *>("scientificCurveDialog") == nullptr,
              "Curve window stayed open");
      }
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
      if (file == "netcdf4.nc") {
        const auto savedCurve = panel->curve();
        const auto savedView = panel->view();
        const QRect mapGeometry = canvas->geometry();
        auto button = window.findChild<QToolButton *>("scientificPanelButton");
        auto close = window.findChild<QToolButton *>("floatingPanelClose");
        check(button && close && button->isChecked(),
              "Scientific floating panel controls unavailable");
        QTest::mouseClick(close, Qt::LeftButton);
        check(!panel->isVisible() && !button->isChecked(),
              "Shared close button did not dismiss scientific panel");
        QTest::mouseClick(button, Qt::LeftButton);
        check(panel->isVisible() && panel->layerId() == id &&
                  panel->curve() == savedCurve && panel->view() == savedView,
              "Reopening scientific panel lost the variable, slice or curve");
        check(canvas->geometry() == mapGeometry,
              "Opening scientific panel resized the map like a dock");
        QTest::mouseClick(button, Qt::LeftButton);
        check(!panel->isVisible(), "Toolbar button did not toggle panel closed");
        QTest::mouseClick(button, Qt::LeftButton);
        window.setActivePanel("layers");
        check(!panel->isVisible(), "Layer and scientific panels overlap");
        window.setActivePanel("scientific");
        check(panel->isVisible() && panel->curve() == savedCurve,
              "Panel switching discarded the time series");
        click(window, "scientificPlay");
        QTest::mouseClick(close, Qt::LeftButton);
        QTest::qWait(650);
        check(panel->view() == savedView, "Playback continued in a closed panel");
        QTest::mouseClick(button, Qt::LeftButton);
        check(window.findChild<QPushButton *>("scientificPlay")->text() == "播放",
              "Reopened scientific panel retained a running playback state");
        controller.setLanguage("en_US");
        QTest::qWait(100);
        check(window.findChild<ScientificPanel *>() == panel && panel->isVisible() &&
                  panel->curve() == savedCurve,
              "Rebuilding floating chrome discarded scientific state");
        controller.setLanguage("zh_CN");
        QTest::qWait(100);
        check(window.findChild<QWidget *>("floatingPanel")->isAncestorOf(panel),
              "Scientific panel detached after language change");
        window.grab().save("tests/output/scientific-floating-panel.png");
      }
      qInfo() << "PASS native open/select/map-click/curve/slice/playback"
              << file;
    }
    check(automaticResets > 0 && canvas->coordinateMode() == "pixel" &&
              controller.layerModel()->count() == 1,
          "Unreferenced HDF4 fixture was not isolated from geographic layers");
    // Rebuild a compatible geographic stack for the reorder/delete checks.
    // The HDF4 journey above intentionally replaced the previous scene.
    for (const auto &file : QStringList{"netcdf-classic.nc", "netcdf4.nc", "scientific.h5"}) {
      controller.loadFiles({base + file});
      wait([&] { return !controller.pendingMultidimensionalImport().isEmpty(); },
           "Reorder fixture catalog missing");
      auto arrays = controller.pendingMultidimensionalImport().value("arrays").toList();
      auto variables = window.findChild<QTreeWidget *>("scientificVariableTree");
      int selected = -1;
      for (int i = 0; i < arrays.size(); ++i)
        if (arrays[i].toMap().value("name").toString().contains("temperature"))
          selected = i;
      check(selected >= 0 && variables, "Reorder variable missing");
      for (int row = 0; row < variables->topLevelItemCount(); ++row)
        variables->topLevelItem(row)->setCheckState(0, Qt::Unchecked);
      variables->topLevelItem(selected)->setCheckState(0, Qt::Checked);
      variables->setCurrentItem(variables->topLevelItem(selected));
      window.findChild<QLineEdit *>("scientificSourceCrs")->setText("EPSG:4326");
      const int previousImports = imports.count();
      click(window, "scientificAdd");
      wait([&] {
        return imports.count() == previousImports + 1 &&
               !controller.multidimensionalImportBusy() && !panel->busy();
      }, "Reorder fixture import failed");
    }
    importResponses.stop();
    expected = controller.layerModel()->count();
    check(expected == 3 && canvas->coordinateMode() == "geographic",
          "Could not rebuild a compatible geographic test scene");
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
    window.showMetadata(0);
    QTest::qWait(80);
    QDialog *metadataDialog = nullptr;
    for (auto dialog : window.findChildren<QDialog *>())
      if (dialog->isVisible() && dialog->windowTitle().startsWith("图层元数据"))
        metadataDialog = dialog;
    check(metadataDialog, "Metadata dialog unavailable");
    auto metadataTree = metadataDialog->findChild<QTreeWidget *>();
    check(metadataTree, "Metadata table unavailable");
    QTreeWidgetItem *multiline = nullptr;
    for (QTreeWidgetItemIterator it(metadataTree); *it; ++it)
      if ((*it)->text(1).contains('\n')) {
        multiline = *it;
        break;
      }
    check(multiline, "Raster multiline metadata missing");
    metadataTree->scrollToItem(multiline);
    QTest::qWait(80);
    const int explicitHeight = (multiline->text(1).count('\n') + 1) *
                               metadataTree->fontMetrics().lineSpacing();
    check(metadataTree->visualItemRect(multiline).height() >= explicitHeight,
          "Multiline metadata is clipped by a single-height row");
    metadataDialog->grab().save("tests/output/metadata-layout.png");
    for (QTreeWidgetItemIterator it(metadataTree); *it; ++it) {
      const int lines = (*it)->text(1).count('\n') + 1;
      if (lines > 1)
        check(metadataTree->visualItemRect(*it).height() >=
                  lines * metadataTree->fontMetrics().lineSpacing(),
              "Another multiline metadata row remains clipped");
    }
    const QString wrappedText = multiline->text(1) + "\n" +
        QString("LONG_PROJECTION_PARAMETER_WITHOUT_SPACES_").repeated(12);
    multiline->setText(1, wrappedText);
    metadataDialog->resize(520, 620);
    QTest::qWait(100);
    QTextDocument expectedText;
    expectedText.setDefaultFont(metadataTree->font());
    expectedText.setDocumentMargin(0);
    expectedText.setPlainText(wrappedText);
    expectedText.setTextWidth(metadataTree->columnWidth(1) - 16);
    check(metadataTree->visualItemRect(multiline).height() >= expectedText.size().height(),
          "Wrapped metadata height did not follow the column width");
    metadataDialog->grab().save("tests/output/metadata-multiline.png");
    const int narrowHeight = metadataTree->visualItemRect(multiline).height();
    metadataDialog->resize(950, 620);
    QTest::qWait(100);
    check(metadataTree->visualItemRect(multiline).height() < narrowHeight,
          "Metadata rows did not shrink after widening the window");
    QFont larger = metadataTree->font();
    larger.setPointSizeF(larger.pointSizeF() + 4);
    metadataTree->setFont(larger);
    QTest::qWait(100);
    expectedText.setDefaultFont(larger);
    expectedText.setTextWidth(metadataTree->columnWidth(1) - 16);
    check(metadataTree->visualItemRect(multiline).height() >= expectedText.size().height(),
          "Metadata row height did not follow a larger font");
    metadataDialog->close();
    QTest::qWait(40);
    const LayerSnapshot groupTemplate = *controller.layerModel()->layerAt(0);
    LayerSnapshot groupA = groupTemplate;
    groupA.id = QStringLiteral("ui-group-a");
    groupA.datasetId = QStringLiteral("ui-group");
    groupA.datasetName = QStringLiteral("scientific-group.nc");
    groupA.name = QStringLiteral("temperature");
    LayerSnapshot groupB = groupA;
    groupB.id = QStringLiteral("ui-group-b");
    groupB.name = QStringLiteral("quality");
    controller.layerModel()->addLayer(groupA);
    controller.layerModel()->addLayer(groupB);
    window.setActivePanel("layers");
    QTest::qWait(250); // allow any queued language/theme chrome rebuild to finish
    window.setActivePanel("layers");
    QTest::qWait(50);
    auto tree = window.findChild<QTreeWidget *>("layerTree");
    check(tree && tree->isVisible(), "Layer tree unavailable");
    QTreeWidgetItem *group = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->text(0) == QStringLiteral("scientific-group.nc"))
        group = tree->topLevelItem(i);
    check(group && group->childCount() == 2,
          "Scientific variables were not grouped below their source dataset");
    check(group->checkState(0) == Qt::Checked,
          "Selected scientific variables were not checked by default");
    auto layerPanel = window.findChild<QWidget *>("floatingPanel");
    tree->setCurrentItem(tree->topLevelItem(0)->childCount()
        ? tree->topLevelItem(0)->child(0) : tree->topLevelItem(0));
    QTest::qWait(40);
    const int rasterPanelWidth = layerPanel->width();
    tree->setCurrentItem(group->child(1));
    QTest::qWait(40);
    check(layerPanel->width() == rasterPanelWidth,
          "Layer panel width changed when the selected layer type changed");
    const int beforeResize = layerPanel->width();
    const QPointF pressPoint(3, layerPanel->height() / 2);
    const QPointF movePoint(-37, layerPanel->height() / 2);
    QMouseEvent resizePress(QEvent::MouseButtonPress, pressPoint,
                            layerPanel->mapToGlobal(pressPoint.toPoint()),
                            Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(layerPanel, &resizePress);
    QMouseEvent resizeMove(QEvent::MouseMove, movePoint,
                           layerPanel->mapToGlobal(movePoint.toPoint()),
                           Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(layerPanel, &resizeMove);
    QMouseEvent resizeRelease(QEvent::MouseButtonRelease, movePoint,
                              layerPanel->mapToGlobal(movePoint.toPoint()),
                              Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(layerPanel, &resizeRelease);
    QTest::qWait(40);
    check(layerPanel->width() >= beforeResize + 30,
          "Dragging the layer panel left edge did not resize it");
    group->setCheckState(0, Qt::Unchecked);
    QTest::qWait(40);
    check(!controller.layerModel()->layerAt(
              controller.layerModel()->indexOfLayer("ui-group-a"))->visible
          && !controller.layerModel()->layerAt(
              controller.layerModel()->indexOfLayer("ui-group-b"))->visible,
          "Dataset visibility did not update all scientific variables");
    group = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->text(0) == QStringLiteral("scientific-group.nc"))
        group = tree->topLevelItem(i);
    group->setCheckState(0, Qt::Checked);
    QTest::qWait(40);
    group = nullptr;
    for (int i = 0; i < tree->topLevelItemCount(); ++i)
      if (tree->topLevelItem(i)->text(0) == QStringLiteral("scientific-group.nc"))
        group = tree->topLevelItem(i);
    const auto dragLayer = [&](int from, int to, bool cancel = false) {
      QTreeWidgetItem *source = group->child(from);
      QTreeWidgetItem *targetItem = group->child(to);
      tree->setCurrentItem(source);
      const QString draggedId = source->data(0, Qt::UserRole).toString();
      const QString targetId = targetItem->data(0, Qt::UserRole).toString();
      const int count = controller.layerModel()->count();
      const int oldSourceRow = controller.layerModel()->indexOfLayer(draggedId);
      const int oldTargetRow = controller.layerModel()->indexOfLayer(targetId);
      const QPointF target = tree->visualItemRect(targetItem).center();
      std::unique_ptr<QMimeData> data(tree->model()->mimeData(
          {tree->indexFromItem(source)}));
      data->setData("application/x-georeader-layer-id", draggedId.toUtf8());
      // Deliver the same events as the OS; offscreen Qt has no native drag loop.
      QDragEnterEvent enter(target, Qt::MoveAction, data.get(),
                            Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(tree->viewport(), &enter);
      QDragMoveEvent move(target, Qt::MoveAction, data.get(),
                          Qt::LeftButton, Qt::NoModifier);
      QApplication::sendEvent(tree->viewport(), &move);
      if (cancel) {
        QDragLeaveEvent leave;
        QApplication::sendEvent(tree->viewport(), &leave);
      } else {
        QDropEvent drop(target, Qt::MoveAction, data.get(),
                        Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(tree->viewport(), &drop);
        check(drop.isAccepted(), "Layer list rejected its internal drop");
      }
      QTest::qWait(100);
      check(controller.layerModel()->count() == count,
            "Dragging a layer removed rows from the panel");
      const int newSourceRow = controller.layerModel()->indexOfLayer(draggedId);
      const int newTargetRow = controller.layerModel()->indexOfLayer(targetId);
      check(cancel ? newSourceRow == oldSourceRow && newTargetRow == oldTargetRow
                   : newSourceRow == oldTargetRow && newTargetRow == oldSourceRow,
            "Dragging did not update layer order");
      group = nullptr;
      for (int i = 0; i < tree->topLevelItemCount(); ++i)
        if (tree->topLevelItem(i)->text(0) == QStringLiteral("scientific-group.nc"))
          group = tree->topLevelItem(i);
    };
    dragLayer(0, 1);
    dragLayer(0, 1, true);
    auto remove = tree->findChild<QToolButton *>("layerRemoveButton");
    check(remove, "Layer row remove button missing");
    QTest::mouseMove(tree->viewport(), tree->visualItemRect(group->child(0)).center());
    QTest::qWait(50);
    check(remove->isVisible(), "Hover did not reveal remove button");
    QTest::mouseMove(remove, remove->rect().center());
    QTest::qWait(50);
    window.grab().save("tests/output/layer-panel-hover.png");
    const QString previousTheme = controller.qtStyle();
    controller.setQtStyle("Dark");
    QTest::qWait(80);
    window.grab().save("tests/output/layer-panel-dark.png");
    controller.setQtStyle(previousTheme);
    tree->setCurrentItem(tree->topLevelItem(0)->childCount()
        ? tree->topLevelItem(0)->child(0) : tree->topLevelItem(0));
    QTest::qWait(80);
    auto editor = window.findChild<QWidget *>("layerEditor");
    auto scroll = window.findChild<QScrollArea *>("layerPropertyScroll");
    check(editor->width() <= scroll->viewport()->width(),
          "Raster property controls clipped horizontally");
    window.grab().save("tests/output/layer-panel-raster.png");
    QTemporaryDir pixelFiles;
    const QString pixelPath = pixelFiles.filePath("no-georeference.tif");
    auto driver = GetGDALDriverManager()->GetDriverByName("GTiff");
    check(driver, "GTiff fixture driver missing");
    auto raster = driver->Create(pixelPath.toUtf8().constData(), 8, 6, 1,
                                 GDT_Byte, nullptr);
    check(raster, "Could not create pixel-coordinate fixture");
    check(raster->GetRasterBand(1)->Fill(42) == CE_None,
          "Could not fill pixel-coordinate fixture");
    GDALClose(raster);
    const auto answerCoordinateReset = [&](const QString &path, bool accept) {
      bool handled = false;
      bool defaultCancel = false;
      QTimer response;
      response.setSingleShot(true);
      QObject::connect(&response, &QTimer::timeout, &window, [&] {
        auto box = window.findChild<QMessageBox *>("coordinateContextConfirmation");
        if (!box)
          return;
        handled = true;
        defaultCancel = box->defaultButton() == box->button(QMessageBox::Cancel);
        box->grab().save("tests/output/coordinate-mode-confirmation.png");
        if (accept) {
          for (auto button : box->buttons())
            if (box->buttonRole(button) == QMessageBox::AcceptRole) {
              button->click();
              return;
            }
        }
        box->button(QMessageBox::Cancel)->click();
      });
      response.start(0);
      controller.loadFiles({path});
      response.stop();
      check(handled, "Coordinate switch did not present a warning");
      check(defaultCancel, "Coordinate replacement did not default to Cancel");
    };
    const auto previousLayers = controller.layerModel()->snapshots();
    answerCoordinateReset(pixelPath, false);
    check(controller.layerModel()->count() == previousLayers.size() &&
              controller.layerModel()->layerAt(0)->id == previousLayers[0].id &&
              canvas->coordinateMode() == "geographic",
          "Cancel changed existing layers or map coordinates");
    answerCoordinateReset(pixelPath, true);
    check(controller.layerModel()->count() == 1 && canvas->coordinateMode() == "pixel" &&
              controller.layerModel()->layerAt(0)->path == pixelPath && panel->curve().isEmpty(),
          "Confirmed pixel mode retained old layers, curves or geographic coordinates");
    QTest::qWait(250); // Allow the deferred viewport render to start.
    wait([&] { return !canvas->rendering() && !panel->busy(); },
         "Pixel-mode rendering did not settle");
    window.setActivePanel("layers");
    window.grab().save("tests/output/isolated-pixel-mode.png");
    answerCoordinateReset(base + "demo.geojson", false);
    check(controller.layerModel()->count() == 1 && canvas->coordinateMode() == "pixel",
          "Cancel changed the pixel scene while returning to geography");
    answerCoordinateReset(base + "demo.geojson", true);
    check(controller.layerModel()->count() == 1 && canvas->coordinateMode() == "geographic" &&
              controller.layerModel()->layerAt(0)->type == "vector",
          "Returning to geography did not replace the pixel scene");
    qInfo() << "PASS native stable IDs, cancellation, stale-result rejection "
               "and vector regression";
    return 0;
  } catch (const std::exception &e) {
    window.grab().save("tests/output/ui-failure.png");
    qCritical() << "FAIL native UI" << e.what();
    return 1;
  }
}
