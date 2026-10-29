import QtQuick
import QtQuick.Controls

Button {
    id: control
    property string iconName: ""
    property bool active: false
    property bool compact: false
    property bool reduced: false
    // Small status on the item (e.g. download progress); a dot in compact mode.
    property string badge: ""
    implicitHeight: 40
    hoverEnabled: true
    leftPadding: compact ? 0 : 12; rightPadding: compact ? 0 : 12
    Accessible.name: text
    ToolTip.visible: compact && hovered
    ToolTip.text: text
    ToolTip.delay: 300
    onHoveredChanged: if (hovered && !active) sfx.play("hover")

    background: Rectangle {
        radius: Theme.radiusMd
        color: control.active ? Theme.accentSoft : control.down ? Theme.fillHover : control.hovered ? "#0AFFFFFF" : "transparent"
        Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        // Active marker: grows from the middle, with a faint glow.
        Rectangle {
            id: marker
            visible: !control.compact
            x: -1; width: 3; radius: 1.5
            height: control.active ? 18 : 0
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.accent
            Behavior on height { enabled: !control.reduced; NumberAnimation { duration: Theme.slow; easing.type: Easing.OutBack; easing.overshoot: 1.6 } }
        }
        SoftGlow {
            z: -1
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: control.active ? 0.16 : 0
            color: Theme.accent
            radius: parent.radius
            blurMax: 20
            spread: -4
            Behavior on opacity { enabled: !control.reduced; NumberAnimation { duration: Theme.slow } }
        }
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
            scale: control.down ? 0.9 : control.hovered && !control.active ? 1.1 : 1
            Behavior on scale { enabled: !control.reduced; NumberAnimation { duration: Theme.normal; easing.type: Easing.OutBack } }
        }
        Rectangle {
            visible: control.badge !== ""
            anchors.verticalCenter: control.compact ? undefined : parent.verticalCenter
            anchors.right: parent.right
            y: control.compact ? 2 : 0
            x: control.compact ? icon.x + icon.width - 2 : 0
            width: control.compact ? 8 : badgeText.implicitWidth + 12
            height: control.compact ? 8 : 20
            radius: height / 2
            color: Theme.accent
            Text {
                id: badgeText
                visible: !control.compact
                anchors.centerIn: parent
                text: control.badge
                color: Theme.accentInk
                font.family: Theme.font; font.pixelSize: 11; font.weight: Font.Bold
            }
        }
        Text {
            visible: !control.compact
            anchors.left: icon.right; anchors.leftMargin: control.hovered && !control.active ? 14 : 12
            Behavior on anchors.leftMargin { enabled: !control.reduced; NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
            anchors.right: parent.right
            anchors.rightMargin: control.badge !== "" ? 44 : 0
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
