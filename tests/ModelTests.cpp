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

    LayerModel groupedLayers;
    LayerSnapshot roads;
    roads.datasetId = QStringLiteral("city-data");
    roads.datasetName = QStringLiteral("City database");
    roads.name = QStringLiteral("roads");
    LayerSnapshot buildings = roads;
    buildings.name = QStringLiteral("buildings");
    LayerSnapshot elevation;
    elevation.datasetId = QStringLiteral("elevation-data");
    elevation.datasetName = QStringLiteral("Elevation");
    elevation.name = QStringLiteral("DEM");
    groupedLayers.addLayer(roads);
    groupedLayers.addLayer(buildings);
    groupedLayers.addLayer(elevation);

    const QVariantMap cityInfo =
        groupedLayers.datasetInfo(QStringLiteral("city-data"));
    if (!expect(groupedLayers.datasetCount() == 2,
                "Dataset count did not group sibling layers")
        || !expect(cityInfo.value(QStringLiteral("layerCount")).toInt() == 2,
                   "Dataset layer count is incorrect")
        || !expect(cityInfo.value(QStringLiteral("name")).toString()
                       == QStringLiteral("City database"),
                   "Dataset name is incorrect")) {
        return 6;
    }

    groupedLayers.setDatasetVisible(QStringLiteral("city-data"), false);
    if (!expect(!groupedLayers.get(0)
                     .value(QStringLiteral("layerVisible")).toBool()
                && !groupedLayers.get(1)
                        .value(QStringLiteral("layerVisible")).toBool(),
                "Dataset visibility did not update every child layer")) {
        return 7;
    }

    groupedLayers.removeDataset(QStringLiteral("city-data"));
    if (!expect(groupedLayers.count() == 1,
                "Dataset removal did not remove every child layer")
        || !expect(groupedLayers.datasetCount() == 1,
                   "Dataset removal did not update dataset count")
        || !expect(groupedLayers.get(0).value(QStringLiteral("name"))
                       == QStringLiteral("DEM"),
                   "Dataset removal removed an unrelated layer")) {
        return 8;
    }
    return 0;
}
