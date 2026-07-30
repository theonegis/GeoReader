#include "AttributeTableModel.h"
#include "LayerModel.h"

#include <QCoreApplication>

#include <gdal.h>

#include <iostream>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication application(argc, argv);
    GDALAllRegister();

    LayerModel layers;
    LayerSnapshot bottom;
    bottom.name = QStringLiteral("bottom");
    LayerSnapshot middle;
    middle.name = QStringLiteral("middle");
    LayerSnapshot top;
    top.name = QStringLiteral("top");
    layers.addLayer(bottom);
    layers.addLayer(middle);
    layers.addLayer(top);
    layers.moveLayer(2, 0);
    if (!expect(layers.get(0).value(QStringLiteral("name")).toString()
                    == QStringLiteral("top"),
                "Layer move did not update model order")) {
        return 1;
    }

    LayerSnapshot attributes;
    attributes.name = QStringLiteral("attributes");
    attributes.path =
        QStringLiteral(GEOREADER_TEST_DATA_DIR "/attribute-table.geojson");
    attributes.sourceLayer = QStringLiteral("attribute_table");
    attributes.type = QStringLiteral("vector");
    layers.addLayer(attributes);

    AttributeTableModel table(&layers);
    if (!expect(table.loadLayer(3), "Attribute table could not be loaded")
        || !expect(table.rowCount() == 3, "Unexpected attribute row count")
        || !expect(table.columnCount() == 4,
                   "Unexpected attribute column count")) {
        return 2;
    }

    table.sortByColumn(3, true);
    if (!expect(table.data(table.index(0, 3),
                           AttributeTableModel::DisplayRole).toString()
                    == QStringLiteral("2"),
                "Numeric attribute sorting failed")) {
        return 3;
    }

    table.setFilter(2, QStringLiteral("west"));
    if (!expect(table.rowCount() == 2, "Attribute filtering failed"))
        return 4;

    table.setFilter(1, QStringLiteral("beta"));
    if (!expect(table.rowCount() == 1, "Case-insensitive filtering failed")
        || !expect(table.data(table.index(0, 1),
                              AttributeTableModel::DisplayRole).toString()
                       == QStringLiteral("Beta"),
                   "Filtered feature is incorrect")) {
        return 5;
    }
    return 0;
}
