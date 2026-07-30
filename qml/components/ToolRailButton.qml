import QtQuick
import QtQuick.Controls

ToolButton {
    id: control
    property string iconName
    property bool selected: false
    property string description
    implicitWidth: 40
    implicitHeight: 40
    hoverEnabled: true
    padding: 8

    contentItem: AppIcon {
        name: control.iconName
        color: control.selected ? "#ffffff"
                                : (control.hovered ? control.palette.text
                                                   : control.palette.mid)
    }

    background: Rectangle {
        radius: 7
        color: control.selected ? "#2475E9"
                                : (control.hovered ? Qt.rgba(
                                      control.palette.text.r,
                                      control.palette.text.g,
                                      control.palette.text.b, .08)
                                                   : "transparent")
        border.width: control.activeFocus ? 1 : 0
        border.color: "#5B9BF3"
    }

    ToolTip.visible: hovered
    ToolTip.text: description
    ToolTip.delay: 500
}
