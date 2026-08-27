#include "AppController.h"
#include "AttributeTableModel.h"
#include "LayerModel.h"

#include <QApplication>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gdal.h>
#include <ogrsf_frmts.h>

#include <iostream>
#include <memory>

namespace {

bool expect(bool condition, const char *message)
{
    if (!condition)
        std::cerr << message << '\n';
    return condition;
}

bool addPointLayer(GDALDataset *dataset, const char *name,
                   double longitude, double latitude,
                   const char *label)
{
    OGRSpatialReference spatialReference;
    spatialReference.SetWellKnownGeogCS("WGS84");
    spatialReference.SetAxisMappingStrategy(OAMS_TRADITIONAL_GIS_ORDER);
    OGRLayer *layer = dataset->CreateLayer(
        name, &spatialReference, wkbPoint, nullptr);
    if (!layer)
        return false;
    OGRFieldDefn field("label", OFTString);
    if (layer->CreateField(&field) != OGRERR_NONE)
        return false;
    using FeaturePtr =
        std::unique_ptr<OGRFeature, decltype(&OGRFeature::DestroyFeature)>;
    FeaturePtr feature(OGRFeature::CreateFeature(layer->GetLayerDefn()),
                       OGRFeature::DestroyFeature);
    feature->SetField("label", label);
    OGRPoint point(longitude, latitude);
    feature->SetGeometry(&point);
    return layer->CreateFeature(feature.get()) == OGRERR_NONE;
}

bool createMultiLayerGeoPackage(const QString &path)
{
    GDALDriver *driver = GetGDALDriverManager()->GetDriverByName("GPKG");
    if (!driver)
        return false;
    using DatasetPtr =
        std::unique_ptr<GDALDataset, decltype(&GDALClose)>;
    DatasetPtr dataset(driver->Create(path.toUtf8().constData(),
                                      0, 0, 0, GDT_Unknown, nullptr),
                       GDALClose);
    return dataset
        && addPointLayer(dataset.get(), "cities", 120.1, 30.2, "alpha")
        && addPointLayer(dataset.get(), "stations", 121.3, 31.4, "beta");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication application(argc, argv);
    GDALAllRegister();

    LayerModel layers;
    LayerSnapshot bottom;
    bottom.name = QStringLiteral("bottom");
    bottom.datasetId = QStringLiteral("stack");
    LayerSnapshot middle;
    middle.name = QStringLiteral("middle");
    middle.datasetId = QStringLiteral("stack");
    LayerSnapshot top;
    top.name = QStringLiteral("top");
    top.datasetId = QStringLiteral("stack");
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

    LayerSnapshot raster;
    raster.name = QStringLiteral("raster");
    raster.type = QStringLiteral("raster");
    raster.bandCount = 3;
    raster.bandMinimums = {0.0, 10.0, 20.0};
    raster.bandMaximums = {100.0, 110.0, 120.0};
    layers.addLayer(raster);

    layers.setRasterStyle(4, QStringLiteral("singleband"), 8, -1, 2, 3,
                          QStringLiteral("Magma"), true,
                          QStringLiteral("percent_clip"));
    QVariantMap rasterState = layers.get(4);
    if (!expect(rasterState.value(QStringLiteral("rasterMode")).toString()
                    == QStringLiteral("singleband"),
                "Raster mode was not updated")
        || !expect(rasterState.value(QStringLiteral("redBand")).toInt() == 3
                       && rasterState.value(QStringLiteral("greenBand")).toInt() == 1
                       && rasterState.value(QStringLiteral("blueBand")).toInt() == 2,
                   "Raster band selection was not clamped")
        || !expect(rasterState.value(QStringLiteral("grayBand")).toInt() == 3,
                   "Gray band was not updated")
        || !expect(rasterState.value(QStringLiteral("colorRamp")).toString()
                       == QStringLiteral("Magma")
                       && rasterState.value(QStringLiteral("colorRampReversed")).toBool(),
                   "Raster color ramp was not updated")
        || !expect(rasterState.value(QStringLiteral("stretchMode")).toString()
                       == QStringLiteral("percent_clip"),
                   "Raster stretch mode was not updated")) {
        return 2;
    }

    layers.setBandRange(4, 2, 12.5, 98.5);
    rasterState = layers.get(4);
    const QVariantList minimums =
        rasterState.value(QStringLiteral("bandMinimums")).toList();
    const QVariantList maximums =
        rasterState.value(QStringLiteral("bandMaximums")).toList();
    if (!expect(minimums.at(1).toDouble() == 12.5
                    && maximums.at(1).toDouble() == 98.5,
                "Raster band range was not updated")) {
        return 3;
    }

    layers.setRasterNoData(4, true, QStringLiteral(" NaN "));
    rasterState = layers.get(4);
    if (!expect(rasterState.value(QStringLiteral("noDataEnabled")).toBool()
                    && rasterState.value(QStringLiteral("noDataValue")).toString()
                           == QStringLiteral("nan"),
                "NaN NoData value was not normalized")) {
        return 4;
    }
    layers.setRasterNoData(4, true, QStringLiteral("invalid"));
    if (!expect(layers.get(4).value(QStringLiteral("noDataValue")).toString()
                    == QStringLiteral("nan"),
                "Invalid enabled NoData value was accepted")) {
        return 5;
    }
    layers.setRasterNoData(4, false, QStringLiteral("invalid"));
    rasterState = layers.get(4);
    if (!expect(!rasterState.value(QStringLiteral("noDataEnabled")).toBool()
                    && rasterState.value(QStringLiteral("noDataValue")).toString()
                           == QStringLiteral("nan"),
                "Disabled NoData state was not normalized")) {
        return 6;
    }

    AttributeTableModel table(&layers);
    if (!expect(table.loadLayer(3), "Attribute table could not be loaded")
        || !expect(table.rowCount() == 3, "Unexpected attribute row count")
        || !expect(table.columnCount() == 4,
                   "Unexpected attribute column count")) {
        return 7;
    }

    table.sortByColumn(3, true);
    if (!expect(table.data(table.index(0, 3),
                           AttributeTableModel::DisplayRole).toString()
                    == QStringLiteral("2"),
                "Numeric attribute sorting failed")) {
        return 8;
    }

    table.setFilter(2, QStringLiteral("west"));
    if (!expect(table.rowCount() == 2, "Attribute filtering failed"))
        return 9;

    table.setFilter(1, QStringLiteral("beta"));
    if (!expect(table.rowCount() == 1, "Case-insensitive filtering failed")
        || !expect(table.data(table.index(0, 1),
                              AttributeTableModel::DisplayRole).toString()
                       == QStringLiteral("Beta"),
                   "Filtered feature is incorrect")) {
        return 10;
    }

    QTemporaryDir temporaryDirectory;
    const QString packagePath =
        temporaryDirectory.filePath(QStringLiteral("multi_layers.gpkg"));
    if (!expect(temporaryDirectory.isValid()
                    && createMultiLayerGeoPackage(packagePath),
                "Could not create multi-layer GeoPackage fixture")) {
        return 11;
    }

    AppController controller;
    controller.loadFiles({packagePath});
    LayerModel *packageLayers = controller.layerModel();
    if (!expect(packageLayers->count() == 2,
                "GeoPackage child layers were not all imported")
        || !expect(packageLayers->layerAt(0)->sourceLayer
                       == QStringLiteral("cities")
                       && packageLayers->layerAt(1)->sourceLayer
                              == QStringLiteral("stations"),
                   "GeoPackage source layer names were not preserved")
        || !expect(packageLayers->layerAt(0)->name
                       == QStringLiteral("multi_layers · cities")
                       && packageLayers->layerAt(1)->name
                              == QStringLiteral("multi_layers · stations"),
                   "GeoPackage child layer display names are ambiguous")) {
        return 12;
    }

    QVariantMap identified = controller.queryVector(1, 121.3, 31.4, 0.01);
    if (!expect(identified.value(QStringLiteral("layer")).toString()
                    == QStringLiteral("multi_layers · stations"),
                "Vector identify selected the wrong GeoPackage child layer")) {
        return 13;
    }

    packageLayers->moveLayer(1, 0);
    identified = controller.queryVector(0, 121.3, 31.4, 0.01);
    if (!expect(packageLayers->layerAt(0)->sourceLayer
                    == QStringLiteral("stations")
                && identified.value(QStringLiteral("layer")).toString()
                       == QStringLiteral("multi_layers · stations"),
                "Reordering lost the GeoPackage source-layer association")) {
        return 14;
    }

    if (!expect(controller.attributeTableModel()->loadLayer(0)
                    && controller.attributeTableModel()->layerName()
                           == QStringLiteral("multi_layers · stations"),
                "Attribute table opened the wrong GeoPackage child layer")) {
        return 15;
    }

    LayerModel groupedLayers;
    LayerSnapshot roads;
    roads.id = QStringLiteral("roads");
    roads.datasetId = QStringLiteral("city-data");
    roads.datasetName = QStringLiteral("City database");
    roads.name = QStringLiteral("roads");
    LayerSnapshot buildings = roads;
    buildings.id = QStringLiteral("buildings");
    buildings.name = QStringLiteral("buildings");
    LayerSnapshot elevation;
    elevation.id = QStringLiteral("elevation");
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

    if (!expect(groupedLayers.moveLayerById(QStringLiteral("buildings"),
                                             QStringLiteral("roads")),
                "Sibling layers could not be reordered by stable ID")
        || !expect(groupedLayers.get(0).value(QStringLiteral("name"))
                       == QStringLiteral("buildings"),
                   "Sibling layer order is incorrect")
        || !expect(!groupedLayers.moveLayerById(QStringLiteral("buildings"),
                                                QStringLiteral("elevation")),
                   "A layer was incorrectly detached from its dataset")) {
        return 7;
    }

    if (!expect(groupedLayers.moveDataset(QStringLiteral("elevation-data"),
                                           QStringLiteral("city-data")),
                "Dataset group could not be reordered")
        || !expect(groupedLayers.get(0).value(QStringLiteral("name"))
                       == QStringLiteral("DEM"),
                   "Dataset group order is incorrect")
        || !expect(groupedLayers.get(1).value(QStringLiteral("datasetId"))
                       == QStringLiteral("city-data")
                   && groupedLayers.get(2).value(QStringLiteral("datasetId"))
                          == QStringLiteral("city-data"),
                   "Dataset children stopped being contiguous after reorder")) {
        return 8;
    }

    groupedLayers.setDatasetVisible(QStringLiteral("city-data"), false);
    if (!expect(!groupedLayers.get(1)
                     .value(QStringLiteral("layerVisible")).toBool()
                && !groupedLayers.get(2)
                        .value(QStringLiteral("layerVisible")).toBool(),
                "Dataset visibility did not update every child layer")) {
        return 9;
    }

    groupedLayers.removeDataset(QStringLiteral("city-data"));
    if (!expect(groupedLayers.count() == 1,
                "Dataset removal did not remove every child layer")
        || !expect(groupedLayers.datasetCount() == 1,
                   "Dataset removal did not update dataset count")
        || !expect(groupedLayers.get(0).value(QStringLiteral("name"))
                       == QStringLiteral("DEM"),
                   "Dataset removal removed an unrelated layer")) {
        return 10;
    }
    return 0;
}
