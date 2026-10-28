import QtQuick
import QtQuick.Controls

Switch {
    id: control
    property bool reduced: false
    padding: 0
    implicitWidth: 42; implicitHeight: 24
    hoverEnabled: true
    indicator: Rectangle {
        implicitWidth: 42; implicitHeight: 24
        radius: 12
        color: control.checked ? Theme.accent : control.hovered ? "#2EFFFFFF" : "#21FFFFFF"
        Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: 15; color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
        Rectangle {
            x: control.checked ? parent.width - width - 3 : 3
            y: 3; width: 18; height: 18; radius: 9
            color: control.checked ? Theme.accentInk : "#E6E8E7"
            Behavior on x { NumberAnimation { duration: control.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
