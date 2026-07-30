import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: emptyState
    property string title: qsTr("打开空间数据")
    property string description: qsTr("支持 Shapefile、GeoJSON、GeoPackage 和 GeoTIFF")
    signal openRequested()
    spacing: 10

    Rectangle {
        Layout.alignment: Qt.AlignHCenter
        width: 52
        height: 52
        radius: 9
        color: Qt.rgba(emptyState.palette.text.r, emptyState.palette.text.g,
                       emptyState.palette.text.b, .08)
        AppIcon {
            anchors.centerIn: parent
            width: 25
            height: 25
            name: "folder"
            color: "#2475E9"
        }
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: emptyState.title
        font.pixelSize: 17
        font.weight: Font.DemiBold
    }
    Label {
        Layout.alignment: Qt.AlignHCenter
        text: emptyState.description
        color: palette.mid
        font.pixelSize: 12
    }
    Button {
        Layout.alignment: Qt.AlignHCenter
        Layout.topMargin: 4
        text: qsTr("选择文件…")
        highlighted: true
        onClicked: emptyState.openRequested()
    }
}

