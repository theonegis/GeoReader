pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import GeoReader
import "components"

ApplicationWindow {
    id: window
    width: 1280
    height: 820
    minimumWidth: 900
    minimumHeight: 600
    visible: true
    title: qsTr("GeoReader")

    property string activePanel: ""
    property int selectedLayer: -1
    property int selectedVectorLayer: -1
    property var rasterResults: []
    property var vectorResult: ({})
    property var metadataResult: ({})
    property var datasetExpansionState: ({})
    property int shortcutRevision: 0
    property color panelShadow: Qt.rgba(0, 0, 0, .18)
    readonly property real mapOverlayRightMargin:
        activePanel === "layers" ? layerPanel.width + 24
        : (activePanel === "raster" ? rasterPanel.width + 24
        : (activePanel === "vector" ? vectorPanel.width + 24
        : (activePanel === "settings" ? settingsPanel.width + 24 : 14)))
    readonly property var baseMapOptions: [
        { text: qsTr("OpenStreetMap 标准地图"), value: "osm" },
        { text: qsTr("Esri 世界影像"), value: "esri_imagery" },
        { text: qsTr("OpenTopoMap 地形图"), value: "opentopomap" }
    ]

    onActivePanelChanged: {
        mapCanvas.rectangleZoomActive = false
        if (activePanel === "vector") {
            mapCanvas.inspectionMode = "vector"
            ensureVectorLayerSelection()
        } else if (activePanel === "raster") {
            mapCanvas.inspectionMode = "raster"
        } else {
            mapCanvas.inspectionMode = "pan"
        }
    }

    function firstVisibleVectorLayer() {
        for (let row = 0; row < app.layerModel.count; ++row) {
            const layer = app.layerModel.get(row)
            if (layer.layerType === "vector" && layer.layerVisible)
                return row
        }
        return -1
    }

    function ensureVectorLayerSelection() {
        if (window.selectedVectorLayer >= 0) {
            const selected = app.layerModel.get(window.selectedVectorLayer)
            if (selected && selected.layerType === "vector"
                    && selected.layerVisible)
                return true
        }
        window.selectedVectorLayer = firstVisibleVectorLayer()
        return window.selectedVectorLayer >= 0
    }

    function bandValue(values, band) {
        if (!values || band < 1 || band > values.length)
            return "0"
        return Number(values[band - 1]).toPrecision(7)
    }

    function bandOptions(count) {
        const options = []
        for (let band = 1; band <= count; ++band)
            options.push(qsTr("波段 %1").arg(band))
        return options
    }

    function longitudeText(value) {
        return Math.abs(value).toFixed(5) + "° " + (value < 0 ? "W" : "E")
    }

    function latitudeText(value) {
        return Math.abs(value).toFixed(5) + "° " + (value < 0 ? "S" : "N")
    }

    function remappedIndex(current, from, to) {
        if (current === from)
            return to
        if (from < to && current > from && current <= to)
            return current - 1
        if (to < from && current >= to && current < from)
            return current + 1
        return current
    }

    function moveLayer(from, to) {
        if (from === to || from < 0 || to < 0)
            return
        // 模型行移动后同步重映射两个选择索引，防止编辑器或识别工具指向
        // 另一个图层。
        selectedLayer = remappedIndex(selectedLayer, from, to)
        selectedVectorLayer = remappedIndex(selectedVectorLayer, from, to)
        app.layerModel.moveLayer(from, to)
        layerList.currentIndex = selectedLayer
    }

    function zoomToLayer(row) {
        const layer = app.layerModel.get(row)
        if (!layer || layer.minLon === undefined)
            return
        selectedLayer = row
        layerList.currentIndex = row
        mapCanvas.fitBounds(layer.minLon, layer.minLat,
                            layer.maxLon, layer.maxLat)
    }

    function datasetIsExpanded(datasetId) {
        datasetExpansionState
        return datasetExpansionState[datasetId] !== false
    }

    function toggleDataset(datasetId) {
        const nextState = Object.assign({}, datasetExpansionState)
        nextState[datasetId] = !datasetIsExpanded(datasetId)
        datasetExpansionState = nextState
    }

    function datasetTint() {
        return Qt.rgba(palette.highlight.r, palette.highlight.g,
                       palette.highlight.b, .065)
    }

    function baseMapDisplayName(value) {
        for (let index = 0; index < baseMapOptions.length; ++index) {
            if (baseMapOptions[index].value === value)
                return baseMapOptions[index].text
        }
        return baseMapOptions[0].text
    }

    function showMetadata(row) {
        metadataResult = app.layerMetadata(row)
        metadataDialog.x = Math.max(20, (window.width - metadataDialog.width) / 2)
        metadataDialog.y = Math.max(20, (window.height - metadataDialog.height) / 2)
        metadataDialog.visible = true
    }

    function showAttributeTable(row) {
        if (!app.attributeTableModel.loadLayer(row))
            return
        attributeTableDialog.sortColumn = -1
        attributeTableDialog.sortAscending = true
        attributeFilter.text = ""
        attributeTableDialog.x =
                Math.max(20, (window.width - attributeTableDialog.width) / 2)
        attributeTableDialog.y =
                Math.max(20, (window.height - attributeTableDialog.height) / 2)
        attributeTableDialog.visible = true
    }

    function updateVectorSelection(longitude, latitude) {
        if (!ensureVectorLayerSelection())
            return
        const tolerance = 360 / (256 * Math.pow(2, mapCanvas.zoomLevel)) * 12
        const result = app.queryVector(
            window.selectedVectorLayer, longitude, latitude, tolerance)
        window.vectorResult = result
        if (result.geometryWkt)
            mapCanvas.setSelectedFeatureWkt(result.geometryWkt)
        else
            mapCanvas.clearSelectedFeature()
    }

    function togglePanel(name) {
        const nextPanel = activePanel === name ? "" : name
        if (activePanel === "vector" && nextPanel !== "vector")
            mapCanvas.clearSelectedFeature()
        activePanel = nextPanel
    }

    function shortcutFor(action) {
        shortcutRevision
        return app.shortcut(action)
    }

    Shortcut {
        sequence: window.shortcutFor("open")
        onActivated: app.openFiles()
    }
    Shortcut {
        sequence: window.shortcutFor("zoomIn")
        onActivated: mapCanvas.zoomBy(1)
    }
    Shortcut {
        sequence: window.shortcutFor("zoomOut")
        onActivated: mapCanvas.zoomBy(-1)
    }
    Shortcut {
        sequence: window.shortcutFor("fit")
        onActivated: {
            if (window.selectedLayer >= 0) {
                const item = app.layerModel.get(window.selectedLayer)
                mapCanvas.fitBounds(item.minLon, item.minLat,
                                    item.maxLon, item.maxLat)
            }
        }
    }
    Shortcut {
        sequence: window.shortcutFor("pan")
        onActivated: {
            window.activePanel = ""
            mapCanvas.forceActiveFocus()
        }
    }
    Shortcut {
        sequence: "Escape"
        enabled: mapCanvas.rectangleZoomActive
        onActivated: mapCanvas.rectangleZoomActive = false
    }

    Connections {
        target: app
        function onLayerAdded(minLon, minLat, maxLon, maxLat) {
            mapCanvas.fitBounds(minLon, minLat, maxLon, maxLat)
            if (window.selectedLayer < 0)
                window.selectedLayer = app.layerModel.count - 1
            if (window.activePanel === "vector")
                window.ensureVectorLayerSelection()
        }
        function onShortcutsChanged() {
            window.shortcutRevision += 1
        }
    }

    MapCanvas {
        id: mapCanvas
        objectName: "mapCanvas"
        anchors.fill: parent
        layerModel: app.layerModel

        onMapClicked: function(longitude, latitude) {
            if (window.activePanel === "vector")
                window.updateVectorSelection(longitude, latitude)
        }
        onRenderError: function(message) {
            renderErrorLabel.text = message
            renderErrorPopup.open()
        }
    }

    Timer {
        id: rasterHoverTimer
        interval: 45
        repeat: true
        running: window.activePanel === "raster"
        triggeredOnStart: true
        property bool dirty: true
        property real longitude: 0
        property real latitude: 0
        onTriggered: {
            if (window.activePanel === "raster" && dirty) {
                dirty = false
                window.rasterResults = app.queryRasters(longitude, latitude)
            }
        }
    }

    Connections {
        target: mapCanvas
        function onMouseCoordinateChanged() {
            if (window.activePanel !== "raster")
                return
            rasterHoverTimer.longitude = mapCanvas.mouseLongitude
            rasterHoverTimer.latitude = mapCanvas.mouseLatitude
            rasterHoverTimer.dirty = true
        }
    }

    EmptyState {
        anchors.centerIn: parent
        visible: app.layerModel.count === 0
        onOpenRequested: app.openFiles()
    }

    Rectangle {
        id: toolRail
        x: 12
        y: 12
        width: 52
        height: parent.height - 24
        radius: 9
        color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b,
                       app.toolBarOpacity)
        border.width: 1
        border.color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, .14)

        Column {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 6
            spacing: 4

            ToolRailButton {
                iconName: "folder"
                description: qsTr("打开文件 (%1)").arg(window.shortcutFor("open"))
                onClicked: app.openFiles()
            }
            ToolRailButton {
                iconName: "layers"
                description: qsTr("图层管理")
                selected: window.activePanel === "layers"
                onClicked: window.togglePanel("layers")
            }
            ToolRailButton {
                iconName: "raster"
                description: qsTr("栅格值查看")
                selected: window.activePanel === "raster"
                onClicked: window.togglePanel("raster")
            }
            ToolRailButton {
                iconName: "table"
                description: qsTr("矢量属性查看")
                selected: window.activePanel === "vector"
                onClicked: window.togglePanel("vector")
            }
        }

        ToolRailButton {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            iconName: "settings"
            description: qsTr("设置")
            selected: window.activePanel === "settings"
            onClicked: window.togglePanel("settings")
        }
    }

    Column {
        anchors.left: toolRail.right
        anchors.leftMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 54
        spacing: 5

        ToolButton {
            width: 36
            height: 36
            onClicked: mapCanvas.zoomBy(1)
            contentItem: AppIcon {
                name: "plus"
                color: parent.palette.text
            }
            background: Rectangle {
                radius: 7
                color: Qt.rgba(parent.palette.window.r, parent.palette.window.g,
                               parent.palette.window.b, .96)
                border.width: 1
                border.color: Qt.rgba(parent.palette.text.r, parent.palette.text.g,
                                      parent.palette.text.b, .14)
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("放大")
        }
        ToolButton {
            width: 36
            height: 36
            onClicked: mapCanvas.zoomBy(-1)
            contentItem: AppIcon {
                name: "minus"
                color: parent.palette.text
            }
            background: Rectangle {
                radius: 7
                color: Qt.rgba(parent.palette.window.r, parent.palette.window.g,
                               parent.palette.window.b, .96)
                border.width: 1
                border.color: Qt.rgba(parent.palette.text.r, parent.palette.text.g,
                                      parent.palette.text.b, .14)
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("缩小")
        }
        ToolButton {
            id: rectangleZoomButton
            width: 36
            height: 36
            onClicked: mapCanvas.rectangleZoomActive =
                           !mapCanvas.rectangleZoomActive
            contentItem: AppIcon {
                name: "viewfinder"
                color: mapCanvas.rectangleZoomActive ? "#ffffff"
                                                     : rectangleZoomButton.palette.text
            }
            background: Rectangle {
                radius: 7
                color: mapCanvas.rectangleZoomActive
                       ? "#2475E9"
                       : Qt.rgba(rectangleZoomButton.palette.window.r,
                                 rectangleZoomButton.palette.window.g,
                                 rectangleZoomButton.palette.window.b, .96)
                border.width: 1
                border.color: mapCanvas.rectangleZoomActive
                              ? "#2475E9"
                              : Qt.rgba(rectangleZoomButton.palette.text.r,
                                        rectangleZoomButton.palette.text.g,
                                        rectangleZoomButton.palette.text.b, .14)
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("矩形框选缩放（Esc 取消）")
        }
    }

    FloatingPanel {
        id: layerPanel
        visible: window.activePanel === "layers"
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        title: qsTr("图层")
        subtitle: app.layerModel.count === 0
                  ? qsTr("尚未加载空间数据")
                  : qsTr("%1 个数据 · %2 个图层")
                    .arg(app.layerModel.datasetCount)
                    .arg(app.layerModel.count)
        onCloseRequested: window.activePanel = ""

        body: Item {
            anchors.fill: parent

            GridLayout {
                anchors.fill: parent
                anchors.margins: 10
                columns: 1
                rowSpacing: 8

                Item {
                    id: baseMapGroup
                    property bool expanded: true
                    Layout.row: 1
                    Layout.fillWidth: true
                    Layout.preferredHeight: expanded
                                            ? 62 + window.baseMapOptions.length * 44
                                            : 62
                    clip: true

                    Behavior on Layout.preferredHeight {
                        NumberAnimation {
                            duration: 150
                            easing.type: Easing.OutCubic
                        }
                    }

                    Rectangle {
                        anchors.fill: parent
                        anchors.topMargin: 8
                        radius: 7
                        color: window.datasetTint()
                        border.width: 1
                        border.color: Qt.rgba(palette.highlight.r,
                                              palette.highlight.g,
                                              palette.highlight.b, .18)
                    }

                    Item {
                        id: baseMapGroupHeader
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        height: 54

                        AppIcon {
                            id: baseMapGroupIcon
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 20
                            height: 20
                            name: "layers"
                            color: palette.highlight
                        }

                        Column {
                            anchors.left: baseMapGroupIcon.right
                            anchors.leftMargin: 9
                            anchors.right: baseMapCollapseButton.left
                            anchors.rightMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 1
                            Label {
                                width: parent.width
                                text: qsTr("底图")
                                font.weight: Font.DemiBold
                                font.pixelSize: 12
                            }
                            Label {
                                width: parent.width
                                text: window.baseMapDisplayName(mapCanvas.baseMap)
                                color: palette.mid
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        ToolButton {
                            id: baseMapCollapseButton
                            anchors.right: parent.right
                            anchors.rightMargin: 7
                            anchors.verticalCenter: parent.verticalCenter
                            implicitWidth: 30
                            implicitHeight: 30
                            onClicked: baseMapGroup.expanded =
                                           !baseMapGroup.expanded
                            contentItem: AppIcon {
                                name: "chevron"
                                color: palette.mid
                                rotation: baseMapGroup.expanded ? 90 : 0
                                Behavior on rotation {
                                    NumberAnimation {
                                        duration: 140
                                        easing.type: Easing.OutCubic
                                    }
                                }
                            }
                            ToolTip.visible: hovered
                            ToolTip.text: baseMapGroup.expanded
                                          ? qsTr("折叠底图") : qsTr("展开底图")
                        }
                    }

                    Column {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: baseMapGroupHeader.bottom
                        visible: baseMapGroup.expanded
                        spacing: 0

                        Repeater {
                            model: window.baseMapOptions
                            delegate: Rectangle {
                                required property var modelData
                                width: parent.width
                                height: 44
                                radius: 6
                                color: mapCanvas.baseMap === modelData.value
                                       ? Qt.rgba(palette.highlight.r,
                                                 palette.highlight.g,
                                                 palette.highlight.b, .13)
                                       : Qt.rgba(palette.window.r,
                                                 palette.window.g,
                                                 palette.window.b, .72)

                                Rectangle {
                                    anchors.left: parent.left
                                    anchors.top: parent.top
                                    anchors.bottom: parent.bottom
                                    width: 3
                                    radius: 1.5
                                    visible: mapCanvas.baseMap
                                             === parent.modelData.value
                                    color: palette.highlight
                                }

                                RadioButton {
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    text: parent.modelData.text
                                    checked: mapCanvas.baseMap
                                             === parent.modelData.value
                                    onClicked: mapCanvas.baseMap =
                                                   parent.modelData.value
                                }
                            }
                        }
                    }
                }

            ListView {
                id: layerList
                Layout.row: 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                spacing: 0
                model: app.layerModel
                currentIndex: window.selectedLayer
                ScrollBar.vertical: ScrollBar {}
                section.property: "datasetId"
                section.criteria: ViewSection.FullString
                section.labelPositioning: ViewSection.InlineLabels

                section.delegate: Item {
                    id: datasetHeader
                    required property string section
                    width: ListView.view.width
                    height: 62
                    readonly property bool expanded:
                        window.datasetIsExpanded(section)

                    property var info: {
                        // revision 使整组显隐、移除图层后，分组标题中的数量与
                        // 三态复选框立即刷新。
                        const modelRevision = app.layerModel.revision
                        return app.layerModel.datasetInfo(section)
                    }

                    Rectangle {
                        id: datasetHeaderBackground
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 8
                        anchors.bottom: parent.bottom
                        radius: 7
                        color: window.datasetTint()
                        border.width: 1
                        border.color: Qt.rgba(palette.highlight.r,
                                              palette.highlight.g,
                                              palette.highlight.b, .18)
                    }

                    CheckBox {
                        id: datasetVisibilityCheck
                        anchors.left: parent.left
                        anchors.leftMargin: 7
                        anchors.verticalCenter:
                            datasetHeaderBackground.verticalCenter
                        tristate: true
                        checkState: parent.info.allVisible
                                    ? Qt.Checked
                                    : (parent.info.anyVisible
                                       ? Qt.PartiallyChecked
                                       : Qt.Unchecked)
                        nextCheckState: function() {
                            return checkState === Qt.Checked
                                    ? Qt.Unchecked : Qt.Checked
                        }
                        onClicked: app.layerModel.setDatasetVisible(
                                       parent.section,
                                       checkState === Qt.Checked)
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("显示或隐藏整个数据")
                    }

                    AppIcon {
                        id: datasetIcon
                        anchors.left: datasetVisibilityCheck.right
                        anchors.leftMargin: 3
                        anchors.verticalCenter:
                            datasetHeaderBackground.verticalCenter
                        width: 20
                        height: 20
                        name: "folder"
                        color: palette.highlight
                    }

                    Column {
                        anchors.left: datasetIcon.right
                        anchors.leftMargin: 8
                        anchors.right: collapseDatasetButton.left
                        anchors.rightMargin: 7
                        anchors.verticalCenter:
                            datasetHeaderBackground.verticalCenter
                        spacing: 1

                        Label {
                            id: datasetNameLabel
                            width: parent.width
                            text: datasetHeader.info.name || qsTr("未命名数据")
                            font.weight: Font.DemiBold
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                            HoverHandler { id: datasetNameHover }
                            ToolTip.visible: datasetNameHover.hovered
                            ToolTip.delay: 500
                            ToolTip.text: datasetHeader.info.name || ""
                        }
                        Label {
                            width: parent.width
                            text: qsTr("%1 个图层").arg(
                                      datasetHeader.info.layerCount || 0)
                            color: palette.mid
                            font.pixelSize: 10
                        }
                    }

                    ToolButton {
                        id: collapseDatasetButton
                        anchors.right: removeDatasetButton.left
                        anchors.rightMargin: 2
                        anchors.verticalCenter:
                            datasetHeaderBackground.verticalCenter
                        implicitWidth: 30
                        implicitHeight: 30
                        onClicked: window.toggleDataset(datasetHeader.section)
                        contentItem: AppIcon {
                            name: "chevron"
                            color: palette.mid
                            rotation: datasetHeader.expanded ? 90 : 0
                            Behavior on rotation {
                                NumberAnimation {
                                    duration: 140
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: datasetHeader.expanded
                                      ? qsTr("折叠图层") : qsTr("展开图层")
                    }

                    ToolButton {
                        id: removeDatasetButton
                        anchors.right: parent.right
                        anchors.rightMargin: 7
                        anchors.verticalCenter:
                            datasetHeaderBackground.verticalCenter
                        implicitWidth: 32
                        implicitHeight: 30
                        contentItem: AppIcon {
                            name: "trash"
                            color: removeDatasetButton.hovered
                                   ? "#D94A4A" : palette.mid
                        }
                        onClicked: {
                            app.layerModel.removeDataset(parent.section)
                            window.selectedLayer = -1
                            window.selectedVectorLayer = -1
                            window.vectorResult = ({})
                            mapCanvas.clearSelectedFeature()
                        }
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("移除整个数据")
                    }
                }

                delegate: Rectangle {
                    id: layerDelegate
                    required property int index
                    required property string datasetId
                    required property string datasetName
                    required property string name
                    required property string layerType
                    required property string geometryType
                    required property bool layerVisible
                    required property real layerOpacity
                    required property color lineColor
                    required property color fillColor
                    required property real lineWidth
                    required property int bandCount
                    required property int redBand
                    required property int greenBand
                    required property int blueBand
                    required property int grayBand
                    required property string rasterMode
                    required property string colorRamp
                    required property bool colorRampReversed
                    required property string stretchMode
                    required property var bandMinimums
                    required property var bandMaximums
                    required property bool noDataEnabled
                    required property string noDataValue
                    required property string crs

                    Drag.active: reorderHandler.active
                    Drag.source: layerDelegate
                    Drag.hotSpot.x: width / 2
                    Drag.hotSpot.y: 30
                    z: reorderHandler.active ? 20 : 0
                    opacity: reorderHandler.active ? .88 : 1

                    property var groupInfo: {
                        const modelRevision = app.layerModel.revision
                        return app.layerModel.datasetInfo(datasetId)
                    }
                    readonly property bool datasetExpanded:
                        window.datasetIsExpanded(datasetId)
                    readonly property bool isLastInDataset:
                        index === groupInfo.firstRow + groupInfo.layerCount - 1

                    function applyRasterDisplay() {
                        const singleBand = layerDelegate.bandCount === 1
                        if (stretchModeBox.currentValue === "minmax"
                                && !singleBand) {
                            app.layerModel.setBandRange(
                                layerDelegate.index, redBandBox.currentIndex + 1,
                                Number(redMinField.text),
                                Number(redMaxField.text))
                            app.layerModel.setBandRange(
                                layerDelegate.index, greenBandBox.currentIndex + 1,
                                Number(greenMinField.text),
                                Number(greenMaxField.text))
                            app.layerModel.setBandRange(
                                layerDelegate.index, blueBandBox.currentIndex + 1,
                                Number(blueMinField.text),
                                Number(blueMaxField.text))
                        } else if (stretchModeBox.currentValue === "minmax") {
                            app.layerModel.setBandRange(
                                layerDelegate.index, 1,
                                Number(grayMinField.text),
                                Number(grayMaxField.text))
                        }
                        app.layerModel.setRasterNoData(
                            layerDelegate.index, noDataCheck.checked,
                            noDataField.text)
                        app.layerModel.setRasterStyle(
                            layerDelegate.index,
                            singleBand ? "single" : "rgb",
                            singleBand ? 1 : redBandBox.currentIndex + 1,
                            singleBand ? 1 : greenBandBox.currentIndex + 1,
                            singleBand ? 1 : blueBandBox.currentIndex + 1,
                            1, singleBand ? rampBox.currentText
                                          : layerDelegate.colorRamp,
                            singleBand && rampReverseCheck.checked,
                            stretchModeBox.currentValue)
                    }

                    readonly property real expandedHeight:
                        (ListView.isCurrentItem
                         ? layerHeader.height + layerEditor.implicitHeight + 24
                         : 62) + (isLastInDataset ? 8 : 0)
                    width: ListView.view.width
                    height: datasetExpanded ? expandedHeight : 0
                    visible: height > 0
                    enabled: datasetExpanded
                    color: window.datasetTint()
                    clip: true

                    Behavior on height {
                        NumberAnimation { duration: 150; easing.type: Easing.OutCubic }
                    }

                    Rectangle {
                        id: layerCard
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 3
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin:
                            layerDelegate.isLastInDataset ? 8 : 3
                        radius: 7
                        color: layerDelegate.ListView.isCurrentItem
                               ? Qt.rgba(palette.highlight.r,
                                         palette.highlight.g,
                                         palette.highlight.b, .13)
                               : Qt.rgba(palette.window.r, palette.window.g,
                                         palette.window.b, .72)
                        border.width: layerDelegate.ListView.isCurrentItem ? 1 : 0
                        border.color: Qt.rgba(palette.highlight.r,
                                              palette.highlight.g,
                                              palette.highlight.b, .28)
                    }

                    Item {
                        id: layerHeader
                        width: parent.width
                        y: 3
                        height: 62
                        HoverHandler { id: layerHeaderHover }
                        ToolTip.visible: layerHeaderHover.hovered
                                         && layerNameLabel.truncated
                        ToolTip.delay: 500
                        ToolTip.text: layerDelegate.name

                        CheckBox {
                            id: visibilityCheck
                            anchors.left: parent.left
                            anchors.leftMargin: 8
                            anchors.verticalCenter: parent.verticalCenter
                            checked: layerDelegate.layerVisible
                            onToggled: app.layerModel.setVisible(
                                           layerDelegate.index, checked)
                        }

                        Rectangle {
                            anchors.left: visibilityCheck.right
                            anchors.leftMargin: 4
                            anchors.verticalCenter: parent.verticalCenter
                            width: 28
                            height: 28
                            radius: 5
                            color: layerDelegate.layerType === "raster"
                                   ? "#6C5CE7"
                                   : (layerDelegate.geometryType === "polygon"
                                      || layerDelegate.geometryType === "point"
                                      ? layerDelegate.fillColor : "transparent")
                            border.width: layerDelegate.layerType === "vector" ? 2 : 0
                            border.color: layerDelegate.lineColor
                            AppIcon {
                                anchors.centerIn: parent
                                width: 16
                                height: 16
                                visible: layerDelegate.layerType === "raster"
                                name: "raster"
                                color: "white"
                            }
                        }

                        Column {
                            anchors.left: visibilityCheck.right
                            anchors.leftMargin: 40
                            anchors.right: reorderGrip.left
                            anchors.rightMargin: 6
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 2
                            Label {
                                id: layerNameLabel
                                width: parent.width
                                text: layerDelegate.name
                                elide: Text.ElideMiddle
                                font.weight: Font.DemiBold
                                font.pixelSize: 12
                            }
                            Label {
                                width: parent.width
                                text: (layerDelegate.layerType === "raster"
                                       ? qsTr("栅格 · %1 波段").arg(layerDelegate.bandCount)
                                       : qsTr("矢量 · %1").arg(
                                             layerDelegate.geometryType === "point"
                                             ? qsTr("点")
                                             : (layerDelegate.geometryType === "polygon"
                                                ? qsTr("面") : qsTr("线"))))
                                      + " · " + layerDelegate.crs
                                color: palette.mid
                                font.pixelSize: 10
                                elide: Text.ElideRight
                            }
                        }

                        AppIcon {
                            id: chevron
                            anchors.right: parent.right
                            anchors.rightMargin: 12
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            name: "chevron"
                            color: palette.mid
                            rotation: layerDelegate.ListView.isCurrentItem ? 90 : 0
                            Behavior on rotation { NumberAnimation { duration: 140 } }
                        }

                        Item {
                            id: reorderGrip
                            anchors.right: chevron.left
                            anchors.rightMargin: 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 24
                            height: 34

                            Column {
                                anchors.centerIn: parent
                                spacing: 3
                                Repeater {
                                    model: 3
                                    Rectangle {
                                        width: 13
                                        height: 1
                                        radius: 1
                                        color: reorderHandler.active
                                               ? palette.highlight : palette.mid
                                    }
                                }
                            }

                            DragHandler {
                                id: reorderHandler
                                target: layerDelegate
                                xAxis.enabled: false
                                onActiveChanged: {
                                    layerList.interactive = !active
                                }
                            }

                            ToolTip.visible: gripHover.hovered
                            ToolTip.text: qsTr("在当前数据内拖动调整图层顺序")
                            HoverHandler { id: gripHover }
                        }

                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onTapped: {
                                window.selectedLayer = layerDelegate.index
                                layerList.currentIndex = layerDelegate.index
                            }
                        }

                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: {
                                window.selectedLayer = layerDelegate.index
                                layerList.currentIndex = layerDelegate.index
                                layerContextMenu.popup()
                            }
                        }

                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.top: layerCard.top
                        anchors.bottom: layerCard.bottom
                        width: 3
                        radius: 1.5
                        visible: layerDelegate.ListView.isCurrentItem
                        color: palette.highlight
                    }

                    Menu {
                        id: layerContextMenu
                        MenuItem {
                            text: qsTr("缩放至图层")
                            onTriggered:
                                window.zoomToLayer(layerDelegate.index)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: qsTr("打开属性表")
                            visible: layerDelegate.layerType === "vector"
                            onTriggered:
                                window.showAttributeTable(layerDelegate.index)
                        }
                        MenuItem {
                            text: qsTr("元信息")
                            onTriggered:
                                window.showMetadata(layerDelegate.index)
                        }
                    }

                    DropArea {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: parent.top
                        anchors.topMargin: 3
                        anchors.bottom: parent.bottom
                        anchors.bottomMargin:
                            layerDelegate.isLastInDataset ? 8 : 3
                        onEntered: function(drag) {
                            if (drag.source
                                    && drag.source !== layerDelegate
                                    && drag.source.datasetId
                                       === layerDelegate.datasetId
                                    && drag.source.index
                                       !== layerDelegate.index) {
                                window.moveLayer(drag.source.index,
                                                 layerDelegate.index)
                            }
                        }
                    }

                    ColumnLayout {
                        id: layerEditor
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.top: layerHeader.bottom
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        anchors.topMargin: 12
                        spacing: 8
                        visible: layerDelegate.ListView.isCurrentItem

                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: qsTr("不透明度"); font.pixelSize: 11 }
                            Slider {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 40
                                from: 0
                                to: 1
                                value: layerDelegate.layerOpacity
                                onMoved: app.layerModel.setOpacity(
                                             layerDelegate.index, value)
                            }
                            Label {
                                text: Math.round(layerDelegate.layerOpacity * 100) + "%"
                                font.pixelSize: 10
                                Layout.preferredWidth: 34
                                Layout.minimumWidth: 34
                                Layout.maximumWidth: 34
                                horizontalAlignment: Text.AlignRight
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "vector"
                            Label {
                                text: qsTr("线色")
                                font.pixelSize: 11
                                visible: layerDelegate.geometryType !== "point"
                            }
                            Rectangle {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: 28
                                radius: 5
                                color: layerDelegate.lineColor
                                visible: layerDelegate.geometryType !== "point"
                                border.width: 1
                                border.color: Qt.darker(layerDelegate.lineColor, 1.25)
                                TapHandler {
                                    onTapped: {
                                        colorDialog.targetRow = layerDelegate.index
                                        colorDialog.targetKind = "line"
                                        colorDialog.selectedColor = layerDelegate.lineColor
                                        colorDialog.open()
                                    }
                                }
                            }
                            Label {
                                text: layerDelegate.geometryType === "point"
                                      ? qsTr("点色") : qsTr("填充")
                                font.pixelSize: 11
                                visible: layerDelegate.geometryType === "polygon"
                                         || layerDelegate.geometryType === "point"
                            }
                            Rectangle {
                                Layout.preferredWidth: 54
                                Layout.preferredHeight: 28
                                radius: 5
                                color: layerDelegate.fillColor
                                visible: layerDelegate.geometryType === "polygon"
                                         || layerDelegate.geometryType === "point"
                                border.width: 1
                                border.color: Qt.darker(layerDelegate.fillColor, 1.25)
                                TapHandler {
                                    onTapped: {
                                        colorDialog.targetRow = layerDelegate.index
                                        colorDialog.targetKind = "fill"
                                        colorDialog.selectedColor = layerDelegate.fillColor
                                        colorDialog.open()
                                    }
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "vector"
                                     && layerDelegate.geometryType !== "point"
                            Label { text: qsTr("线宽"); font.pixelSize: 11 }
                            Slider {
                                Layout.fillWidth: true
                                from: .5
                                to: 8
                                value: layerDelegate.lineWidth
                                onMoved: app.layerModel.setVectorStyle(
                                             layerDelegate.index,
                                             layerDelegate.lineColor,
                                             layerDelegate.fillColor, value)
                            }
                            Label {
                                text: layerDelegate.lineWidth.toFixed(1) + " px"
                                font.pixelSize: 10
                                Layout.preferredWidth: 44
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "raster"
                            Label {
                                text: qsTr("拉伸方式")
                                font.pixelSize: 11
                            }
                            ComboBox {
                                id: stretchModeBox
                                Layout.fillWidth: true
                                textRole: "text"
                                valueRole: "value"
                                model: [
                                    {
                                        text: qsTr("最小值–最大值"),
                                        value: "minmax"
                                    },
                                    {
                                        text: qsTr("2%–98% 累计裁剪"),
                                        value: "percent_clip"
                                    },
                                    {
                                        text: qsTr("均值 ±2σ"),
                                        value: "standard_deviation"
                                    },
                                    {
                                        text: qsTr("直方图均衡化"),
                                        value: "histogram_equalization"
                                    }
                                ]
                                Component.onCompleted: {
                                    for (let option = 0;
                                         option < model.length; ++option) {
                                        if (model[option].value
                                                === layerDelegate.stretchMode) {
                                            currentIndex = option
                                            break
                                        }
                                    }
                                }
                                onActivated: layerDelegate.applyRasterDisplay()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "raster"
                                     && layerDelegate.bandCount > 1
                            spacing: 5

                            Label {
                                text: qsTr("RGB 波段")
                                font.pixelSize: 11
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "R"
                                    color: "#E64A4A"
                                    font.weight: Font.DemiBold
                                    Layout.preferredWidth: 20
                                }
                                Label {
                                    text: qsTr("红色通道")
                                    font.pixelSize: 10
                                    color: palette.mid
                                    Layout.preferredWidth: 64
                                }
                                ComboBox {
                                    id: redBandBox
                                    Layout.fillWidth: true
                                    model: window.bandOptions(
                                               layerDelegate.bandCount)
                                    currentIndex: Math.max(
                                                      0,
                                                      layerDelegate.redBand - 1)
                                    onActivated:
                                        layerDelegate.applyRasterDisplay()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "G"
                                    color: "#2E9D58"
                                    font.weight: Font.DemiBold
                                    Layout.preferredWidth: 20
                                }
                                Label {
                                    text: qsTr("绿色通道")
                                    font.pixelSize: 10
                                    color: palette.mid
                                    Layout.preferredWidth: 64
                                }
                                ComboBox {
                                    id: greenBandBox
                                    Layout.fillWidth: true
                                    model: window.bandOptions(
                                               layerDelegate.bandCount)
                                    currentIndex: Math.max(
                                                      0,
                                                      layerDelegate.greenBand - 1)
                                    onActivated:
                                        layerDelegate.applyRasterDisplay()
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: "B"
                                    color: "#3578D4"
                                    font.weight: Font.DemiBold
                                    Layout.preferredWidth: 20
                                }
                                Label {
                                    text: qsTr("蓝色通道")
                                    font.pixelSize: 10
                                    color: palette.mid
                                    Layout.preferredWidth: 64
                                }
                                ComboBox {
                                    id: blueBandBox
                                    Layout.fillWidth: true
                                    model: window.bandOptions(
                                               layerDelegate.bandCount)
                                    currentIndex: Math.max(
                                                      0,
                                                      layerDelegate.blueBand - 1)
                                    onActivated:
                                        layerDelegate.applyRasterDisplay()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "raster"
                                     && layerDelegate.bandCount === 1
                            Label { text: qsTr("色带"); font.pixelSize: 11 }
                            ComboBox {
                                id: rampBox
                                Layout.fillWidth: true
                                model: ["Viridis", "Plasma", "Inferno", "Magma",
                                        "Cividis", "Turbo", "Terrain", "Gray"]
                                currentIndex: Math.max(0, model.indexOf(
                                                           layerDelegate.colorRamp))
                                onActivated: layerDelegate.applyRasterDisplay()
                            }
                            CheckBox {
                                id: rampReverseCheck
                                text: qsTr("反向")
                                checked: layerDelegate.colorRampReversed
                                onToggled: layerDelegate.applyRasterDisplay()
                            }
                        }

                        GridLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "raster"
                                     && stretchModeBox.currentValue === "minmax"
                            columns: 5
                            columnSpacing: 6
                            rowSpacing: 5

                            Label {
                                text: qsTr("手动范围")
                                font.pixelSize: 11
                            }
                            Label {
                                text: qsTr("波段")
                                color: palette.mid
                                font.pixelSize: 10
                            }
                            Label {
                                text: qsTr("最小值")
                                color: palette.mid
                                font.pixelSize: 10
                            }
                            Label {
                                text: qsTr("最大值")
                                color: palette.mid
                                font.pixelSize: 10
                            }
                            Item { Layout.fillWidth: true }

                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "R"
                                color: "#E64A4A"
                                font.weight: Font.DemiBold
                            }
                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "B" + (redBandBox.currentIndex + 1)
                                font.pixelSize: 10
                            }
                            TextField {
                                id: redMinField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMinimums,
                                          redBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            TextField {
                                id: redMaxField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMaximums,
                                          redBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            Item {
                                visible: layerDelegate.bandCount > 1
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "G"
                                color: "#2E9D58"
                                font.weight: Font.DemiBold
                            }
                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "B" + (greenBandBox.currentIndex + 1)
                                font.pixelSize: 10
                            }
                            TextField {
                                id: greenMinField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMinimums,
                                          greenBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            TextField {
                                id: greenMaxField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMaximums,
                                          greenBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            Item {
                                visible: layerDelegate.bandCount > 1
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "B"
                                color: "#3578D4"
                                font.weight: Font.DemiBold
                            }
                            Label {
                                visible: layerDelegate.bandCount > 1
                                text: "B" + (blueBandBox.currentIndex + 1)
                                font.pixelSize: 10
                            }
                            TextField {
                                id: blueMinField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMinimums,
                                          blueBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            TextField {
                                id: blueMaxField
                                visible: layerDelegate.bandCount > 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMaximums,
                                          blueBandBox.currentIndex + 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            Item {
                                visible: layerDelegate.bandCount > 1
                                Layout.fillWidth: true
                            }

                            Label {
                                visible: layerDelegate.bandCount === 1
                                text: qsTr("范围")
                                font.pixelSize: 11
                            }
                            Label {
                                visible: layerDelegate.bandCount === 1
                                text: "B1"
                                font.pixelSize: 10
                            }
                            TextField {
                                id: grayMinField
                                visible: layerDelegate.bandCount === 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMinimums, 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            TextField {
                                id: grayMaxField
                                visible: layerDelegate.bandCount === 1
                                Layout.preferredWidth: 92
                                text: window.bandValue(
                                          layerDelegate.bandMaximums, 1)
                                selectByMouse: true
                                font.pixelSize: 10
                                validator: DoubleValidator {}
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                            Item {
                                visible: layerDelegate.bandCount === 1
                                Layout.fillWidth: true
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            visible: layerDelegate.layerType === "raster"
                            CheckBox {
                                id: noDataCheck
                                text: qsTr("NoData 透明")
                                checked: layerDelegate.noDataEnabled
                                font.pixelSize: 11
                                onToggled:
                                    layerDelegate.applyRasterDisplay()
                            }
                            TextField {
                                id: noDataField
                                Layout.fillWidth: true
                                enabled: noDataCheck.checked
                                text: layerDelegate.noDataValue
                                placeholderText: "nan"
                                selectByMouse: true
                                font.pixelSize: 10
                                ToolTip.visible: hovered
                                ToolTip.text: qsTr("可输入 nan 或数值")
                                onEditingFinished:
                                    layerDelegate.applyRasterDisplay()
                            }
                        }

                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: layerList.count === 0
                    text: qsTr("点击左侧打开按钮添加数据")
                    color: palette.mid
                }
            }
            }
        }
    }

    FloatingPanel {
        id: rasterPanel
        visible: window.activePanel === "raster"
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        title: qsTr("栅格值")
        subtitle: qsTr("移动鼠标，实时读取所有可见栅格像元")
        onCloseRequested: window.activePanel = ""

        body: ScrollView {
            anchors.fill: parent
            anchors.margins: 12
            clip: true

            Column {
                width: parent.width
                spacing: 8

                Label {
                    visible: window.rasterResults.length === 0
                    width: parent.width
                    topPadding: 24
                    text: app.layerModel.count === 0
                          ? qsTr("请先加载 GeoTIFF 数据")
                          : qsTr("将鼠标移动到地图数据上开始查询")
                    color: palette.mid
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                }

                Repeater {
                    model: window.rasterResults
                    delegate: Rectangle {
                        required property var modelData
                        width: parent.width
                        height: resultColumn.implicitHeight + 22
                        radius: 7
                        color: Qt.rgba(palette.text.r, palette.text.g,
                                       palette.text.b, .045)

                        Column {
                            id: resultColumn
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.margins: 11
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: 5
                            Label {
                                text: modelData.name
                                font.weight: Font.DemiBold
                            }
                            Label {
                                text: qsTr("像元：%1").arg(modelData.pixel)
                                color: palette.mid
                                font.pixelSize: 10
                            }
                            Repeater {
                                model: modelData.values
                                Label {
                                    required property string modelData
                                    text: modelData
                                    font.family: "Menlo"
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    FloatingPanel {
        id: vectorPanel
        visible: window.activePanel === "vector"
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        title: qsTr("矢量属性")
        subtitle: qsTr("单击地图要素进行识别；命中要素会高亮")
        onCloseRequested: {
            window.activePanel = ""
            mapCanvas.clearSelectedFeature()
        }

        body: ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8

            Label {
                text: qsTr("当前查询图层")
                color: palette.mid
                font.pixelSize: 11
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(contentHeight, 142)
                clip: true
                spacing: 4
                model: app.layerModel

                delegate: ItemDelegate {
                    required property int index
                    required property string name
                    required property string layerType
                    width: ListView.view.width
                    height: layerType === "vector" ? 36 : 0
                    visible: layerType === "vector"
                    text: name
                    highlighted: window.selectedVectorLayer === index
                    onClicked: {
                        window.selectedVectorLayer = index
                        window.vectorResult = ({})
                        mapCanvas.clearSelectedFeature()
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, .10)
            }

            Label {
                Layout.fillWidth: true
                visible: !window.vectorResult.fields
                Layout.topMargin: 18
                text: window.selectedVectorLayer < 0
                      ? qsTr("请选择一个矢量图层")
                      : qsTr("在地图上单击以查询要素")
                horizontalAlignment: Text.AlignHCenter
                color: palette.mid
            }

            ColumnLayout {
                visible: !!window.vectorResult.fields
                Layout.fillWidth: true
                spacing: 2
                Label {
                    text: window.vectorResult.layer || ""
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("FID: %1").arg(window.vectorResult.fid ?? "—")
                    color: palette.mid
                    font.pixelSize: 10
                }
            }

            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: window.vectorResult.fields || []
                ScrollBar.vertical: ScrollBar {}

                delegate: Rectangle {
                    required property int index
                    required property var modelData
                    width: ListView.view.width
                    height: Math.max(38, attributeValue.implicitHeight + 16)
                    color: index % 2
                           ? Qt.rgba(palette.text.r, palette.text.g,
                                     palette.text.b, .035) : "transparent"
                    Label {
                        anchors.left: parent.left
                        anchors.leftMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width * .38
                        text: modelData.name
                        color: palette.mid
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                    Label {
                        id: attributeValue
                        anchors.left: parent.left
                        anchors.leftMargin: parent.width * .41
                        anchors.right: parent.right
                        anchors.rightMargin: 8
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.value
                        wrapMode: Text.Wrap
                        font.pixelSize: 11
                    }
                }
            }
        }
    }

    FloatingPanel {
        id: settingsPanel
        visible: window.activePanel === "settings"
        anchors.top: parent.top
        anchors.topMargin: 12
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        anchors.right: parent.right
        anchors.rightMargin: 12
        title: qsTr("设置")
        subtitle: qsTr("语言、界面、Qt Quick 样式与快捷键")
        onCloseRequested: window.activePanel = ""

        body: ScrollView {
            id: settingsScroll
            anchors.fill: parent
            anchors.margins: 14
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 10

                Label {
                    text: qsTr("外观")
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("语言")
                    color: palette.mid
                    font.pixelSize: 11
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: [qsTr("简体中文"), "English"]
                    currentIndex: app.language === "en_US" ? 1 : 0
                    onActivated: app.language =
                                     currentIndex === 1 ? "en_US" : "zh_CN"
                }
                Label { text: qsTr("字体"); color: palette.mid; font.pixelSize: 11 }
                ComboBox {
                    Layout.fillWidth: true
                    editable: true
                    model: Qt.fontFamilies()
                    Component.onCompleted: currentIndex = find(app.fontFamily)
                    onActivated: app.fontFamily = currentText
                    onAccepted: app.fontFamily = editText
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("字号"); color: palette.mid; font.pixelSize: 11 }
                    Slider {
                        Layout.fillWidth: true
                        from: 10
                        to: 22
                        stepSize: 1
                        value: app.fontSize
                        onMoved: app.fontSize = Math.round(value)
                    }
                    Label {
                        text: app.fontSize + " pt"
                        Layout.preferredWidth: 42
                        horizontalAlignment: Text.AlignRight
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: qsTr("工具栏透明度")
                        color: palette.mid
                        font.pixelSize: 11
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0.5
                        to: 1.0
                        stepSize: 0.01
                        value: app.toolBarOpacity
                        onMoved: app.toolBarOpacity = value
                    }
                    Label {
                        text: Math.round(app.toolBarOpacity * 100) + "%"
                        Layout.preferredWidth: 40
                        horizontalAlignment: Text.AlignRight
                    }
                }
                Label {
                    text: qsTr("Qt Quick Style")
                    color: palette.mid
                    font.pixelSize: 11
                }
                ComboBox {
                    id: styleBox
                    Layout.fillWidth: true
                    model: ["macOS", "FluentWinUI3", "Material", "Fusion", "Basic"]
                    Component.onCompleted: currentIndex = Math.max(0, find(app.qtStyle))
                    onActivated: app.setQtStyle(currentText)
                }
                Label {
                    visible: app.restartRequired
                    Layout.fillWidth: true
                    text: qsTr("样式变更将在下次启动 GeoReader 时生效。")
                    wrapMode: Text.Wrap
                    color: "#D48420"
                    font.pixelSize: 10
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.topMargin: 5
                    Layout.preferredHeight: 1
                    color: Qt.rgba(palette.text.r, palette.text.g,
                                   palette.text.b, .10)
                }

                Label {
                    text: qsTr("快捷键")
                    font.weight: Font.DemiBold
                    Layout.topMargin: 4
                }

                Repeater {
                    model: [
                        { key: "open", label: qsTr("打开文件") },
                        { key: "zoomIn", label: qsTr("放大") },
                        { key: "zoomOut", label: qsTr("缩小") },
                        { key: "pan", label: qsTr("平移") },
                        { key: "fit", label: qsTr("适合范围") }
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            text: modelData.label
                            Layout.fillWidth: true
                            font.pixelSize: 11
                        }
                        TextField {
                            Layout.preferredWidth: 112
                            text: window.shortcutFor(modelData.key)
                            horizontalAlignment: Text.AlignHCenter
                            onEditingFinished: app.setShortcut(modelData.key, text)
                        }
                    }
                }

                Button {
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("恢复默认快捷键")
                    onClicked: app.resetShortcuts()
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 1
                    color: Qt.rgba(palette.text.r, palette.text.g,
                                   palette.text.b, .10)
                }

                Label {
                    text: "GeoReader " + app.version
                    color: palette.mid
                    font.pixelSize: 10
                }
                Label {
                    Layout.fillWidth: true
                    text: "Qt · Mapnik · GDAL"
                    color: palette.mid
                    font.pixelSize: 10
                }
                Label {
                    Layout.fillWidth: true
                    Layout.topMargin: 4
                    text: qsTr("版权声明")
                    font.weight: Font.DemiBold
                    font.pixelSize: 11
                }
                Label {
                    Layout.fillWidth: true
                    text: qsTr("本APP由西北大学谭振宇团队开发。用户可以免费分发和使用；商业使用必须获得作者授权。")
                    wrapMode: Text.Wrap
                    color: palette.mid
                    font.pixelSize: 10
                }
            }
        }
    }

    Rectangle {
        anchors.left: toolRail.right
        anchors.leftMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 12
        width: coordinateRow.implicitWidth + 20
        height: 30
        radius: 7
        color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, .94)
        border.width: 1
        border.color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, .13)

        Row {
            id: coordinateRow
            anchors.centerIn: parent
            spacing: 8
            Label {
                text: window.longitudeText(mapCanvas.mouseLongitude)
                      + ", " + window.latitudeText(mapCanvas.mouseLatitude)
                font.family: app.fontFamily
                font.pixelSize: 10
            }
            Rectangle {
                width: 1
                height: 13
                color: Qt.rgba(palette.text.r, palette.text.g, palette.text.b, .16)
            }
            Label {
                text: "Z " + mapCanvas.zoomLevel.toFixed(1)
                color: palette.mid
                font.pixelSize: 10
            }
            BusyIndicator {
                width: 14
                height: 14
                running: mapCanvas.rendering
                visible: running
            }
        }
    }

    Label {
        id: baseMapAttributionLabel
        anchors.right: parent.right
        anchors.rightMargin: window.mapOverlayRightMargin
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 10
        width: Math.min(implicitWidth,
                        window.width - window.mapOverlayRightMargin - 82)
        text: mapCanvas.baseMapAttribution
        color: "#4C5663"
        font.pixelSize: 9
        padding: 4
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignRight
        background: Rectangle {
            radius: 4
            color: Qt.rgba(1, 1, 1, .78)
        }
    }

    Rectangle {
        id: metadataDialog
        // 普通 Item 形式的悬浮窗不占用模态事件循环，标题栏可自由拖动。
        visible: false
        z: 100
        width: Math.min(700, window.width - 48)
        height: Math.min(620, window.height - 48)
        radius: 9
        color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, .98)
        border.width: 1
        border.color: Qt.rgba(palette.text.r, palette.text.g,
                              palette.text.b, .18)
        layer.enabled: true
        layer.samples: 4

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: metadataTitleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 54

                MouseArea {
                    anchors.fill: parent
                    drag.target: metadataDialog
                    drag.minimumX: 0
                    drag.maximumX: Math.max(0, window.width
                                            - metadataDialog.width)
                    drag.minimumY: 0
                    drag.maximumY: Math.max(0, window.height
                                            - metadataDialog.height)
                    cursorShape: pressed ? Qt.ClosedHandCursor
                                         : Qt.OpenHandCursor
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 70
                    spacing: 2
                    Label {
                        text: qsTr("图层元信息")
                        font.weight: Font.DemiBold
                        font.pixelSize: 15
                    }
                    Label {
                        width: parent.width
                        text: window.metadataResult.title || ""
                        color: palette.mid
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    onClicked: metadataDialog.visible = false
                    contentItem: AppIcon {
                        name: "close"
                        color: parent.hovered ? palette.text : palette.mid
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Qt.rgba(palette.text.r, palette.text.g,
                                   palette.text.b, .10)
                }
            }

            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                contentWidth: availableWidth

                Column {
                    width: parent.width
                    padding: 14
                    spacing: 6

                    Repeater {
                        model: window.metadataResult.entries || []
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: parent.width - 28
                            height: metadataEntry.implicitHeight + 20
                            radius: 6
                            color: index % 2
                                   ? Qt.rgba(palette.text.r, palette.text.g,
                                             palette.text.b, .035)
                                   : "transparent"

                            Column {
                                id: metadataEntry
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.margins: 10
                                anchors.verticalCenter: parent.verticalCenter
                                spacing: 3
                                Label {
                                    width: parent.width
                                    text: modelData.section + " · "
                                          + modelData.name
                                    color: palette.mid
                                    font.pixelSize: 10
                                    font.weight: Font.DemiBold
                                }
                                Label {
                                    width: parent.width
                                    text: modelData.value
                                    wrapMode: Text.WrapAnywhere
                                    font.pixelSize: 11
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    Rectangle {
        id: attributeTableDialog
        // 属性表保持非模态：打开后仍可缩放地图或操作右侧图层面板。
        property int sortColumn: -1
        property bool sortAscending: true
        readonly property int tableColumnCount:
            app.attributeTableModel.columns.length
        readonly property real minimumTableColumnWidth: 150

        function tableColumnWidth(column) {
            if (tableColumnCount <= 0)
                return minimumTableColumnWidth

            // 字段较少时均分整个视口；字段较多时保持可读的最小宽度，
            // 超出的列交由 TableView 横向滚动，表头与数据列始终对齐。
            return Math.max(minimumTableColumnWidth,
                            attributeTable.width / tableColumnCount)
        }

        visible: false
        z: 110
        width: Math.min(980, window.width - 48)
        height: Math.min(620, window.height - 48)
        radius: 9
        color: Qt.rgba(palette.window.r, palette.window.g, palette.window.b, .98)
        border.width: 1
        border.color: Qt.rgba(palette.text.r, palette.text.g,
                              palette.text.b, .18)
        layer.enabled: true
        layer.samples: 4

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                id: attributeTitleBar
                Layout.fillWidth: true
                Layout.preferredHeight: 54

                MouseArea {
                    anchors.fill: parent
                    drag.target: attributeTableDialog
                    // 允许主体向左右或下方超出主窗口并由窗口边缘裁剪，但至少保留
                    // 120 px 标题栏宽度和完整标题栏高度，确保可以拖回。
                    drag.minimumX: -attributeTableDialog.width + 120
                    drag.maximumX: window.width - 120
                    drag.minimumY: 0
                    drag.maximumY: window.height - attributeTitleBar.height
                    cursorShape: pressed ? Qt.ClosedHandCursor
                                         : Qt.OpenHandCursor
                }

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 70
                    spacing: 2
                    Label {
                        text: qsTr("属性表")
                        font.weight: Font.DemiBold
                        font.pixelSize: 15
                    }
                    Label {
                        width: parent.width
                        text: app.attributeTableModel.layerName
                        color: palette.mid
                        font.pixelSize: 10
                        elide: Text.ElideMiddle
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    width: 32
                    height: 32
                    onClicked: attributeTableDialog.visible = false
                    contentItem: AppIcon {
                        name: "close"
                        color: parent.hovered ? palette.text : palette.mid
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Qt.rgba(palette.text.r, palette.text.g,
                                   palette.text.b, .10)
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8

                Label {
                    text: qsTr("查询列")
                    color: palette.mid
                    font.pixelSize: 11
                }
                ComboBox {
                    id: attributeFilterColumn
                    Layout.preferredWidth: 190
                    model: app.attributeTableModel.columns
                }
                TextField {
                    id: attributeFilter
                    Layout.fillWidth: true
                    placeholderText: qsTr("输入属性值（包含匹配）")
                    selectByMouse: true
                    onAccepted: app.attributeTableModel.setFilter(
                                    attributeFilterColumn.currentIndex, text)
                }
                Button {
                    text: qsTr("查询")
                    onClicked: app.attributeTableModel.setFilter(
                                   attributeFilterColumn.currentIndex,
                                   attributeFilter.text)
                }
                Button {
                    text: qsTr("清除")
                    onClicked: {
                        attributeFilter.text = ""
                        app.attributeTableModel.setFilter(-1, "")
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: Qt.rgba(palette.text.r, palette.text.g,
                               palette.text.b, .045)
                clip: true

                Row {
                    x: -attributeTable.contentX
                    height: parent.height
                    Repeater {
                        model: app.attributeTableModel.columns
                        delegate: Rectangle {
                            required property int index
                            required property string modelData
                            width: attributeTableDialog.tableColumnWidth(index)
                            height: 38
                            color: attributeTableDialog.sortColumn === index
                                   ? Qt.rgba(palette.accent.r,
                                             palette.accent.g,
                                             palette.accent.b, .10)
                                   : (headerMouse.containsMouse
                                      ? Qt.rgba(palette.text.r,
                                                palette.text.g,
                                                palette.text.b, .035)
                                      : "transparent")

                            Label {
                                anchors.left: parent.left
                                anchors.leftMargin: 10
                                anchors.right: parent.right
                                anchors.rightMargin: 10
                                anchors.verticalCenter: parent.verticalCenter
                                text: modelData
                                      + (attributeTableDialog.sortColumn
                                         === index
                                         ? (attributeTableDialog.sortAscending
                                            ? "  ↑" : "  ↓") : "")
                                color: palette.text
                                elide: Text.ElideRight
                                font.pixelSize: 11
                                font.weight: Font.DemiBold
                            }

                            Rectangle {
                                anchors.right: parent.right
                                width: 1
                                height: parent.height
                                color: Qt.rgba(palette.text.r,
                                               palette.text.g,
                                               palette.text.b, .07)
                            }

                            MouseArea {
                                id: headerMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: {
                                    if (attributeTableDialog.sortColumn
                                            === index) {
                                        attributeTableDialog.sortAscending =
                                            !attributeTableDialog.sortAscending
                                    } else {
                                        attributeTableDialog.sortColumn = index
                                        attributeTableDialog.sortAscending = true
                                    }
                                    app.attributeTableModel.sortByColumn(
                                        index,
                                        attributeTableDialog.sortAscending)
                                }
                            }
                        }
                    }
                }
            }

            TableView {
                id: attributeTable
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                reuseItems: true
                boundsBehavior: Flickable.StopAtBounds
                model: app.attributeTableModel
                columnWidthProvider: function(column) {
                    return attributeTableDialog.tableColumnWidth(column)
                }
                rowHeightProvider: function(row) { return 34 }
                onWidthChanged: forceLayout()
                ScrollBar.vertical: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }
                ScrollBar.horizontal: ScrollBar {
                    policy: ScrollBar.AsNeeded
                }

                Connections {
                    target: app.attributeTableModel
                    function onLayerChanged() {
                        attributeTable.forceLayout()
                    }
                }

                delegate: Rectangle {
                    required property int row
                    required property int column
                    required property string display
                    implicitWidth:
                        attributeTableDialog.tableColumnWidth(column)
                    implicitHeight: 34
                    color: row % 2
                           ? Qt.rgba(palette.text.r, palette.text.g,
                                     palette.text.b, .035)
                           : "transparent"
                    border.width: 1
                    border.color: Qt.rgba(palette.text.r, palette.text.g,
                                          palette.text.b, .06)
                    Label {
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        text: display
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.pixelSize: 11
                        ToolTip.visible: cellHover.hovered
                                         && implicitWidth > width
                        ToolTip.text: display
                        HoverHandler { id: cellHover }
                    }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                color: Qt.rgba(palette.text.r, palette.text.g,
                               palette.text.b, .04)
                Label {
                    anchors.left: parent.left
                    anchors.leftMargin: 12
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("显示 %1 / %2 个要素")
                          .arg(app.attributeTableModel.filteredCount)
                          .arg(app.attributeTableModel.totalCount)
                    color: palette.mid
                    font.pixelSize: 10
                }
            }
        }
    }

    ColorDialog {
        id: colorDialog
        property int targetRow: -1
        property string targetKind: "line"
        title: targetKind === "line" ? qsTr("选择线颜色") : qsTr("选择填充颜色")
        onAccepted: {
            const layer = app.layerModel.get(targetRow)
            app.layerModel.setVectorStyle(
                targetRow,
                targetKind === "line" ? selectedColor : layer.lineColor,
                targetKind === "fill" ? selectedColor : layer.fillColor,
                layer.lineWidth)
        }
    }

    Popup {
        id: renderErrorPopup
        x: (window.width - width) / 2
        y: 20
        width: Math.min(520, window.width - 40)
        padding: 12
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle {
            radius: 7
            color: palette.window
            border.width: 1
            border.color: "#D65A5A"
        }
        contentItem: Label {
            id: renderErrorLabel
            wrapMode: Text.Wrap
            color: "#C84242"
        }
    }
}
