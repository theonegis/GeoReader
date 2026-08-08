#include "MapCanvas.h"

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <QWheelEvent>

#include <cmath>
#include <iostream>

namespace {

class TestMapCanvas final : public MapCanvas
{
public:
    using MapCanvas::MapCanvas;
    using MapCanvas::wheelEvent;
};

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

void sendWheel(TestMapCanvas &canvas, const QPointF &position, int delta)
{
    QWheelEvent event(position, position, {}, QPoint(0, delta),
                      Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    canvas.wheelEvent(&event);
}

bool unchanged(double first, double second)
{
    return std::abs(first - second) < 1e-9;
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication application(argc, argv);

    QQuickWindow window;
    window.setGeometry(0, 0, 480, 320);

    TestMapCanvas canvas(window.contentItem());
    canvas.setPosition({0, 0});
    canvas.setSize({480, 320});

    QQuickItem floatingPanel(window.contentItem());
    floatingPanel.setPosition({240, 20});
    floatingPanel.setSize({220, 280});
    floatingPanel.setZ(10);

    QQuickItem table(&floatingPanel);
    table.setPosition({10, 60});
    table.setSize({200, 200});

    window.show();
    QCoreApplication::processEvents();

    const double initialZoom = canvas.zoomLevel();
    sendWheel(canvas, {100, 100}, 120);
    if (!expect(canvas.zoomLevel() > initialZoom,
                "Wheel over the visible map did not zoom")) {
        return 1;
    }

    const double zoomBeforePanelWheel = canvas.zoomLevel();
    sendWheel(canvas, {300, 140}, 120);
    if (!expect(unchanged(canvas.zoomLevel(), zoomBeforePanelWheel),
                "Wheel over a floating table leaked into MapCanvas")) {
        return 2;
    }

    floatingPanel.setVisible(false);
    QCoreApplication::processEvents();
    sendWheel(canvas, {300, 140}, 120);
    if (!expect(canvas.zoomLevel() > zoomBeforePanelWheel,
                "Hidden overlay incorrectly blocked map zoom")) {
        return 3;
    }

    return 0;
}
