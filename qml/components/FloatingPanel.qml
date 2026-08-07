import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: panel
    property string title
    property string subtitle
    property alias body: bodyContainer.data
    property real preferredWidth: 326
    property real minimumPanelWidth: 286
    readonly property real maximumPanelWidth: parent
                                                   ? parent.width * 2 / 3
                                                   : 860
    signal closeRequested()

    padding: 0
    implicitWidth: 326
    width: Math.min(maximumPanelWidth,
                    Math.max(minimumPanelWidth, preferredWidth))

    background: Rectangle {
        radius: 9
        color: Qt.rgba(panel.palette.window.r, panel.palette.window.g,
                       panel.palette.window.b, .96)
        border.width: 1
        border.color: Qt.rgba(panel.palette.text.r, panel.palette.text.g,
                              panel.palette.text.b, .14)
        layer.enabled: true
        layer.samples: 4
    }

    contentItem: Item {
        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.preferredHeight: panel.subtitle.length > 0 ? 66 : 54

                Column {
                    anchors.left: parent.left
                    anchors.leftMargin: 18
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width - 68
                    spacing: 2
                    Label {
                        text: panel.title
                        font.weight: Font.DemiBold
                        font.pixelSize: 15
                        color: panel.palette.text
                    }
                    Label {
                        width: parent.width
                        visible: panel.subtitle.length > 0
                        text: panel.subtitle
                        font.pixelSize: 11
                        color: panel.palette.mid
                        elide: Text.ElideRight
                    }
                }

                ToolButton {
                    anchors.right: parent.right
                    anchors.rightMargin: 10
                    anchors.verticalCenter: parent.verticalCenter
                    implicitWidth: 32
                    implicitHeight: 32
                    onClicked: panel.closeRequested()
                    contentItem: AppIcon {
                        name: "close"
                        color: parent.hovered ? panel.palette.text
                                              : panel.palette.mid
                    }
                    background: Rectangle {
                        radius: 6
                        color: parent.hovered
                               ? Qt.rgba(panel.palette.text.r,
                                         panel.palette.text.g,
                                         panel.palette.text.b, .08)
                               : "transparent"
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: Qt.rgba(panel.palette.text.r,
                                   panel.palette.text.g,
                                   panel.palette.text.b, .10)
                }
            }

            Item {
                id: bodyContainer
                Layout.fillWidth: true
                Layout.fillHeight: true
            }
        }

        Item {
            id: resizeHandle
            anchors.left: parent.left
            anchors.leftMargin: -5
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            width: 12
            z: 100
            property real widthAtDragStart: panel.width

            Rectangle {
                anchors.left: parent.left
                anchors.leftMargin: 5
                anchors.verticalCenter: parent.verticalCenter
                width: 2
                height: Math.min(58, parent.height * .16)
                radius: 1
                color: resizeHover.hovered || resizeDrag.active
                       ? panel.palette.highlight : "transparent"
                opacity: .72
            }

            HoverHandler {
                id: resizeHover
                cursorShape: Qt.SizeHorCursor
            }

            DragHandler {
                id: resizeDrag
                target: null
                xAxis.enabled: true
                yAxis.enabled: false
                onActiveChanged: {
                    if (active)
                        resizeHandle.widthAtDragStart = panel.width
                }
                onTranslationChanged: {
                    if (!active)
                        return
                    panel.preferredWidth = Math.min(
                                panel.maximumPanelWidth,
                                Math.max(panel.minimumPanelWidth,
                                         resizeHandle.widthAtDragStart
                                         - translation.x))
                }
            }
        }
    }
}
