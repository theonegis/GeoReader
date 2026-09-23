pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    SystemPalette { id: theme }
    required property var controller
    property bool panelVisible: false
    property bool active: true
    property string observedId: ""
    property var lastQuery: ({})
    property string profile: "time"
    function probe(x, y, geo) {
        lastQuery = {x:x, y:y, geographic:geo}
        controller.requestTimeSeries(view.id, x, y, geo, profile)
    }
    function step(delta) {
        const s = view.selection || {}, t = s.time
        if (t === undefined || t < 0) return
        const n = s.dimensions[t].size, i = s.indices[t]
        controller.setScientificSlice(view.id, t, (i + delta + n) % n)
    }
    function display() {
        controller.setScientificDisplay(view.id, ramp.currentText, reverse.checked, fixed.checked, Number(minimum.text), Number(maximum.text))
    }
    property var catalog: controller.scientificCatalog
    property var view: controller.scientificView
    signal activated()
    function showLayer(id) { controller.inspectScientificLayer(id); panelVisible = true; activated() }
    function clickMap(lon, lat) {
        if (panelVisible && view.id) probe(lon, lat, true)
    }
    Connections {
        target: root.controller
        function onScientificCatalogChanged() {
            if (Object.keys(root.catalog).length) picker.open()
            else picker.close()
        }
        function onScientificViewChanged() {
            if (!root.view.id) {root.panelVisible = false;root.observedId = "";return}
            if(root.observedId !== root.view.id) {
                root.observedId = root.view.id;root.lastQuery = ({});root.panelVisible = true;root.activated()
            }
        }
    }
    Dialog {
        id: picker
        objectName: "scientificVariableDialog"
        anchors.centerIn: parent
        width: Math.min(700, root.width - 40)
        height: Math.min(620, root.height - 40)
        modal: true
        title: qsTr("选择科学数据变量")
        closePolicy: Popup.NoAutoClose
        property var variables: (root.catalog.variables || []).filter(function(v) { return !search.text || v.name.toLowerCase().indexOf(search.text.toLowerCase()) >= 0 })
        property var variable: variables[variableBox.currentIndex] || ({})
        property var dimensions: variable.dimensions || []
        property var indices: []
        function resetDimensions() {
            const values = []
            for (let i = 0; i < dimensions.length; ++i) values.push(0)
            indices = values
            xBox.currentIndex = variable.x === undefined ? -1 : variable.x
            yBox.currentIndex = variable.y === undefined ? -1 : variable.y
            timeBox.currentIndex = variable.time === undefined ? 0 : variable.time + 1
        }
        onVariableChanged: Qt.callLater(resetDimensions)
        onOpened: { variableBox.currentIndex = 0; Qt.callLater(resetDimensions) }
        contentItem: ColumnLayout {
            spacing: 12
            Label { Layout.fillWidth: true; text: root.catalog.file || ""; elide: Text.ElideMiddle }
            Label { Layout.fillWidth: true; visible: !!root.catalog.error; text: root.catalog.error || ""; wrapMode: Text.Wrap; color: "#cb493b" }
            TextField { id: search; Layout.fillWidth: true; placeholderText: qsTr("搜索变量或组路径，例如 /science/temperature"); onTextChanged: variableBox.currentIndex = 0 }
            ComboBox {
                id: variableBox
                objectName: "scientificVariableBox"
                Layout.fillWidth: true
                model: picker.variables
                textRole: "name"
                onActivated: picker.resetDimensions()
            }
            Label { text: (picker.variable.description || "") + (picker.variable.unit ? " [" + picker.variable.unit + "]" : ""); Layout.fillWidth: true; wrapMode: Text.Wrap }
            Label { text: qsTr("确认空间轴和时间轴。其他维度固定在指定索引（从 0 开始）。"); Layout.fillWidth: true; wrapMode: Text.Wrap }
            GridLayout {
                columns: 2
                Layout.fillWidth: true
                Label { text: qsTr("X / 列") }
                ComboBox { id: xBox; objectName: "scientificXAxis"; Layout.fillWidth: true; model: picker.dimensions; textRole: "name" }
                Label { text: qsTr("Y / 行") }
                ComboBox { id: yBox; objectName: "scientificYAxis"; Layout.fillWidth: true; model: picker.dimensions; textRole: "name" }
                Label { text: qsTr("时间轴") }
                ComboBox {
                    id: timeBox
                    objectName: "scientificTimeAxis"
                    Layout.fillWidth: true
                    model: [qsTr("无（二维显示）")].concat(picker.dimensions.map(function(d) { return d.name }))
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true; contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    Repeater {
                        model: picker.dimensions
                        delegate: RowLayout {
                            id: dimensionRow
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            Label { text: dimensionRow.modelData.name + " × " + dimensionRow.modelData.size; Layout.fillWidth: true }
                            SpinBox {
                                from: 0; to: Math.min(2147483647, dimensionRow.modelData.size - 1)
                                editable: true
                                enabled: dimensionRow.index !== xBox.currentIndex && dimensionRow.index !== yBox.currentIndex
                                value: picker.indices[dimensionRow.index] || 0
                                onValueModified: { const copy = picker.indices.slice(); copy[dimensionRow.index] = value; picker.indices = copy }
                            }
                        }
                    }
                }
            }
            Label { text: root.controller.statusMessage; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                Button { objectName: "scientificCancel"; text: qsTr("取消"); onClicked: root.controller.cancelScientificSelection() }
                Button {
                    objectName: "scientificAdd"
                    text: qsTr("添加变量")
                    highlighted: true
                    enabled: picker.dimensions.length >= 2 && xBox.currentIndex !== yBox.currentIndex
                             && timeBox.currentIndex - 1 !== xBox.currentIndex && timeBox.currentIndex - 1 !== yBox.currentIndex
                    onClicked: root.controller.selectScientificVariable((root.catalog.variables || []).findIndex(function(v) { return v.name === picker.variable.name }), xBox.currentIndex, yBox.currentIndex, timeBox.currentIndex - 1, picker.indices)
                }
            }
        }
    }
    Rectangle {
        id: panel
        objectName: "scientificTimePanel"
        visible: root.panelVisible && root.active
        x: Math.max(12, root.width - width - 12); y: 16
        width: Math.min(510, root.width - 30)
        height: root.height - 32
        color: theme.window; border.color: theme.mid; radius: 9
        property var selection: root.view.selection || ({})
        property var dimensions: selection.dimensions || []
        ScrollView {
            id: panelScroll
            anchors.fill: parent; anchors.margins: 14
            clip: true; contentWidth: availableWidth
        ColumnLayout {
            width: panelScroll.availableWidth; spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: qsTr("变量与时序"); font.bold: true; Layout.fillWidth: true }
                BusyIndicator { running: root.controller.scientificBusy; Layout.preferredWidth: 24; Layout.preferredHeight: 24 }
                ToolButton { text: "×"; onClicked: root.panelVisible = false }
            }
            Label { text: root.view.name || qsTr("从图层面板选择一个科学数据变量"); Layout.fillWidth: true; elide: Text.ElideMiddle; ToolTip.visible: titleMouse.containsMouse; ToolTip.text: text
                MouseArea { id: titleMouse; anchors.fill: parent; hoverEnabled: true }
            }
            Label { text: root.view.geographic ? qsTr("点击地图像元绘制时序，固定其他维度。"): qsTr("像素视图：未使用地理定位。点击下方图像绘制时序。"); Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 11 }
            Repeater {
                model: panel.dimensions
                delegate: RowLayout {
                    id: sliceRow
                    required property int index
                    required property var modelData
                    visible: index !== panel.selection.x && index !== panel.selection.y
                    Layout.fillWidth: true
                    Label { text: sliceRow.modelData.name + (sliceRow.index === panel.selection.time ? qsTr("（时间）") : ""); Layout.fillWidth: true }
                    SpinBox {
                        objectName: "scientificSlice" + sliceRow.index
                        from: 0; to: Math.min(2147483647, sliceRow.modelData.size - 1); editable: true
                        value: panel.selection.indices ? panel.selection.indices[sliceRow.index] : 0
                        onValueModified: root.controller.setScientificSlice(root.view.id, sliceRow.index, value)
                    }
                    Label { text: "/ " + sliceRow.modelData.size; font.pixelSize: 11 }
                }
            }
            Label { text: root.view.timeLabel || ""; Layout.fillWidth: true; font.pixelSize: 11 }
            RowLayout {
                visible: panel.selection.time !== undefined && panel.selection.time >= 0
                Layout.fillWidth: true
                Button { text: "◀"; enabled: !root.controller.scientificBusy; onClicked: root.step(-1); ToolTip.text: qsTr("上一帧"); ToolTip.visible: hovered }
                Button { id: play; objectName: "scientificPlay"; checkable: true; text: checked ? qsTr("暂停") : qsTr("播放"); onCheckedChanged: if(checked) root.lastQuery = ({}) }
                Button { objectName: "scientificNext"; text: "▶"; enabled: !root.controller.scientificBusy; onClicked: root.step(1); ToolTip.text: qsTr("下一帧"); ToolTip.visible: hovered }
                ComboBox { id: speed; model: ["0.5×", "1×", "2×"]; currentIndex: 1; Layout.fillWidth: true }
                Timer { interval: speed.currentIndex === 0 ? 1400 : speed.currentIndex === 1 ? 700 : 350; repeat: true; running: play.checked && panel.visible; onTriggered: if(!root.controller.scientificBusy) root.step(1) }
            }
            RowLayout {
                Layout.fillWidth: true
                ComboBox { id: ramp; objectName: "scientificRamp"; Layout.fillWidth: true; model: ["Viridis","Plasma","Inferno","Magma","Cividis","Turbo","Terrain","Gray"]; currentIndex: Math.max(0, model.indexOf((panel.selection.display || {}).ramp || "Viridis")); onActivated: root.display() }
                CheckBox { id: reverse; text: qsTr("反转"); checked: (panel.selection.display || {}).reversed || false; onToggled: root.display() }
                CheckBox { id: fixed; text: qsTr("固定范围"); checked: (panel.selection.display || {}).fixed || false; onToggled: root.display() }
            }
            RowLayout {
                visible: fixed.checked
                Layout.fillWidth: true
                TextField { id: minimum; Layout.fillWidth: true; placeholderText: qsTr("最小值"); text: (panel.selection.display || {}).minimum !== undefined ? panel.selection.display.minimum : (root.view.minimum || 0); onEditingFinished: root.display() }
                Label { text: "～" }
                TextField { id: maximum; Layout.fillWidth: true; placeholderText: qsTr("最大值"); text: (panel.selection.display || {}).maximum !== undefined ? panel.selection.display.maximum : (root.view.maximum || 1); onEditingFinished: root.display() }
            }
            Image { Layout.fillWidth: true; Layout.preferredHeight: 12; source: root.view.legend || "" }
            Label { text: qsTr("切片色标：%1 ～ %2 %3").arg(root.view.displayMinimum === undefined || root.view.displayMinimum === null ? "—" : Number(root.view.displayMinimum).toPrecision(5)).arg(root.view.displayMaximum === undefined || root.view.displayMaximum === null ? "—" : Number(root.view.displayMaximum).toPrecision(5)).arg(panel.selection.unit || ""); Layout.fillWidth: true; font.pixelSize: 11 }
            CheckBox { id: pixelToggle; text: qsTr("显示像素视图"); checked: !root.view.geographic }
            Image {
                id: preview
                objectName: "scientificPreview"
                visible: pixelToggle.checked
                Layout.fillWidth: true; Layout.preferredHeight: 180
                source: root.view.image || ""
                fillMode: Image.PreserveAspectFit
                smooth: false
                Rectangle {
                    visible: root.lastQuery.x !== undefined && !root.lastQuery.geographic
                    width: 9; height: 9; color: "transparent"; border.color: "white"; border.width: 2
                    x: (preview.width-preview.paintedWidth)/2 + (Math.floor(root.lastQuery.x || 0)+.5)/Math.max(1,root.view.width || 1)*preview.paintedWidth - width/2
                    y: (preview.height-preview.paintedHeight)/2 + (Math.floor(root.lastQuery.y || 0)+.5)/Math.max(1,root.view.height || 1)*preview.paintedHeight - height/2
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.CrossCursor
                    onClicked: function(mouse) {
                        const dx = (preview.width-preview.paintedWidth)/2, dy = (preview.height-preview.paintedHeight)/2
                        if (preview.paintedWidth <= 0 || mouse.x < dx || mouse.y < dy || mouse.x >= dx+preview.paintedWidth || mouse.y >= dy+preview.paintedHeight) return
                        root.probe((mouse.x-dx)/preview.paintedWidth*root.view.width, (mouse.y-dy)/preview.paintedHeight*root.view.height, false)
                    }
                }
            }
            Label { visible: pixelToggle.checked; text: qsTr("颜色范围：%1 ～ %2").arg(root.view.displayMinimum === undefined || root.view.displayMinimum === null ? "—" : Number(root.view.displayMinimum).toPrecision(5)).arg(root.view.displayMaximum === undefined || root.view.displayMaximum === null ? "—" : Number(root.view.displayMaximum).toPrecision(5)); font.pixelSize: 11 }
            Label { visible: !!root.view.error; text: root.view.error || ""; color: "#cb493b"; Layout.fillWidth: true; wrapMode: Text.Wrap }
            TabBar {
                id: tabs; Layout.fillWidth: true
                TabButton { text: qsTr("曲线") }
                TabButton { text: qsTr("数值") }
                TabButton { text: qsTr("分布") }
                TabButton { text: qsTr("属性") }
            }
            RowLayout {
                visible: tabs.currentIndex === 0
                ComboBox {
                    objectName: "scientificProfile"
                    model: [qsTr("时间曲线"),qsTr("行剖面（X）"),qsTr("列剖面（Y）")]
                    Layout.fillWidth: true
                    onActivated: {root.profile = ["time","row","column"][currentIndex];if(root.lastQuery.x !== undefined) root.probe(root.lastQuery.x,root.lastQuery.y,root.lastQuery.geographic)}
                }
                Button { text: qsTr("导出 CSV"); enabled: (root.controller.timeSeries.points || []).length > 0; onClicked: root.controller.exportTimeSeries() }
            }
            TimeSeriesChart { visible: tabs.currentIndex === 0; Layout.fillWidth: true; Layout.preferredHeight: 230; result: root.controller.timeSeries }
            ListView {
                visible: tabs.currentIndex === 1
                Layout.fillWidth: true; Layout.preferredHeight: 250; clip: true
                model: root.controller.timeSeries.cells || []
                header: Label { text: qsTr("点击像元附近 5×5 邻域 · 列 / 行 / 原始值 / 物理值"); font.pixelSize: 11 }
                delegate: Label {
                    required property var modelData
                    width: ListView.view.width; height: 24; font.pixelSize: 11
                    text: modelData.column + " / " + modelData.row + "     " + (modelData.raw === null ? qsTr("缺测") : modelData.raw + "  →  " + Number(modelData.value).toPrecision(7))
                }
                ScrollBar.vertical: ScrollBar {}
            }
            ColumnLayout {
                visible: tabs.currentIndex === 2
                Layout.fillWidth: true
                Label { text: qsTr("预览抽样统计（非全数据统计）"); font.pixelSize: 11 }
                Label { text: qsTr("有效 %1 / %2 · 均值 %3").arg(root.view.validCount || 0).arg(root.view.sampleCount || 0).arg(root.view.mean === null || root.view.mean === undefined ? "—" : Number(root.view.mean).toPrecision(7)); font.pixelSize: 11 }
                Canvas {
                    id: histogram
                    Layout.fillWidth: true; Layout.preferredHeight: 180
                    property var bins: root.view.histogram || []
                    onBinsChanged: requestPaint()
                    onWidthChanged: requestPaint()
                    onPaint: {let c=getContext("2d");c.clearRect(0,0,width,height);let max=1;for(let v of bins)max=Math.max(max,v);c.fillStyle="#3574dc";for(let i=0;i<bins.length;i++){let h=bins[i]/max*(height-10);c.fillRect(i*width/bins.length,height-h,width/bins.length-1,h)}}
                }
            }
            ScrollView {
                visible: tabs.currentIndex === 3
                Layout.fillWidth: true; Layout.preferredHeight: 250; contentWidth: availableWidth
                TextArea {
                    readOnly: true; wrapMode: Text.WrapAnywhere; font.pixelSize: 11
                    text: (panel.selection.file || "") + "\n" + (panel.selection.array || "") + "\n" + (panel.selection.dataType || "") + "\n" + (panel.selection.attributes || []).map(function(a){return a.name + ": " + a.value}).join("\n")
                }
            }
            Button { text: qsTr("导出预览 PNG"); enabled: !!root.view.image; onClicked: root.controller.exportScientificImage() }
            Label { text: root.controller.statusMessage; Layout.fillWidth: true; wrapMode: Text.Wrap; font.pixelSize: 10 }
        }
        }
    }
}
