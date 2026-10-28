import QtQuick
import QtQuick.Controls

ScrollBar {
    id: control
    padding: 3
    minimumSize: 0.08
    contentItem: Rectangle {
        implicitWidth: 4; implicitHeight: 4
        radius: 2
        color: control.pressed ? "#66FFFFFF" : control.hovered ? "#4DFFFFFF" : "#2EFFFFFF"
        opacity: control.policy === ScrollBar.AlwaysOn || control.active || control.hovered ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.normal } }
    }
}
