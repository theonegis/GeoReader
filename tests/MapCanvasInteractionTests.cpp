#include "MapCanvas.h"

#include <QApplication>
#include <QWheelEvent>
#include <QWidget>

#include <cmath>
#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

void sendWheelAt(QWidget &window, const QPoint &position, int delta)
{
    QWidget *receiver = window.childAt(position);
    if (!receiver)
        receiver = &window;
    const QPoint localPosition = receiver->mapFrom(&window, position);
    const QPoint globalPosition = window.mapToGlobal(position);
    QWheelEvent event(localPosition, globalPosition, {}, QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(receiver, &event);
}

bool unchanged(double first, double second)
{
    return std::abs(first - second) < 1e-9;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);

    QWidget window;
    window.resize(480, 320);

    MapCanvas canvas(&window);
    canvas.setGeometry(0, 0, 480, 320);

    QWidget floatingPanel(&window);
    floatingPanel.setGeometry(240, 20, 220, 280);
    floatingPanel.raise();

    QWidget table(&floatingPanel);
    table.setGeometry(10, 60, 200, 200);

    window.show();
    QCoreApplication::processEvents();

    const double initialZoom = canvas.zoomLevel();
    sendWheelAt(window, {100, 100}, 120);
    if (!expect(canvas.zoomLevel() > initialZoom,
                "Wheel over the visible map did not zoom")) {
        return 1;
    }

    const double zoomBeforePanelWheel = canvas.zoomLevel();
    sendWheelAt(window, {300, 140}, 120);
    if (!expect(unchanged(canvas.zoomLevel(), zoomBeforePanelWheel),
                "Wheel over a floating table leaked into MapCanvas")) {
        return 2;
    }

    floatingPanel.setVisible(false);
    QCoreApplication::processEvents();
    sendWheelAt(window, {300, 140}, 120);
    if (!expect(canvas.zoomLevel() > zoomBeforePanelWheel,
                "Hidden overlay incorrectly blocked map zoom")) {
        return 3;
    }

    const double zoomBeforeExplicitBlock = canvas.zoomLevel();
    canvas.setWheelZoomEnabled(false);
    sendWheelAt(window, {100, 100}, 120);
    if (!expect(unchanged(canvas.zoomLevel(), zoomBeforeExplicitBlock),
                "Explicit panel hover guard did not block map zoom")) {
        return 4;
    }

    canvas.setWheelZoomEnabled(true);
    sendWheelAt(window, {100, 100}, 120);
    if (!expect(canvas.zoomLevel() > zoomBeforeExplicitBlock,
                "Map zoom did not resume after leaving the panel")) {
        return 5;
    }

    return 0;
}
