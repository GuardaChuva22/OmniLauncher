import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string iconName: ""
    property bool active: false
    property bool compact: false
    property bool reduced: false
    implicitHeight: 40
    hoverEnabled: true
    leftPadding: compact ? 0 : 12; rightPadding: compact ? 0 : 12
    Accessible.name: text
    ToolTip.visible: compact && hovered
    ToolTip.text: text
    ToolTip.delay: 300

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.active ? Theme.accentSoft : control.down ? Theme.fillHover : control.hovered ? "#0AFFFFFF" : "transparent"
        Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        Rectangle {
            anchors.fill: parent; anchors.margins: -2
            radius: parent.radius + 2; color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        UiIcon {
            id: icon
            x: control.compact ? (parent.width - width) / 2 : 0
            anchors.verticalCenter: parent.verticalCenter
            width: 18; height: 18
            name: control.iconName
            color: control.active ? Theme.accent : control.hovered ? Theme.secondary : Theme.muted
            Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        }
        Text {
            visible: !control.compact
            anchors.left: icon.right; anchors.leftMargin: 12
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            text: control.text
            elide: Text.ElideRight
            font.family: Theme.font; font.pixelSize: 14
            font.weight: control.active ? Font.DemiBold : Font.Medium
            color: control.active ? Theme.accentText : control.hovered ? Theme.secondary : Theme.muted
            Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        }
    }
}
