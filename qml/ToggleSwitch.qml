import QtQuick
import QtQuick.Controls

// On/off switch. Everything follows one animated `progress` (0 off → 1 on): the
// knob slides, the track blends to the accent and a rose glow fades in around it.
// The knob stretches while pressed, like a finger pushing it.
Switch {
    id: control
    property bool reduced: false
    padding: 0
    implicitWidth: 42; implicitHeight: 24
    hoverEnabled: true
    onToggled: sfx.play(checked ? "toggle-on" : "toggle-off")

    property real progress: checked ? 1 : 0
    Behavior on progress { NumberAnimation { duration: control.reduced ? 0 : 280; easing.type: Easing.OutCubic } }

    indicator: Item {
        implicitWidth: 42; implicitHeight: 24

        SoftGlow {
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: control.progress * (control.hovered ? 0.75 : 0.5) * (control.enabled ? 1 : 0.4)
            color: Theme.accent
            radius: 12
            blurMax: 18
            spread: -1
            Behavior on opacity { enabled: !control.reduced; NumberAnimation { duration: Theme.fast } }
        }
        Rectangle {
            id: track
            anchors.fill: parent
            radius: 12
            color: Qt.tint(control.hovered ? "#2EFFFFFF" : "#21FFFFFF",
                           Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, control.progress))
            border.width: 1
            border.color: Qt.rgba(1, 1, 1, 0.06 * (1 - control.progress))
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: 15; color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
        Rectangle {
            id: knob
            property real stretch: control.pressed ? 5 : 0
            width: 18 + stretch; height: 18
            Behavior on stretch { enabled: !control.reduced; NumberAnimation { duration: 160; easing.type: Easing.OutCubic } }
            x: 3 + (track.width - width - 6) * control.progress
            y: 3
            radius: 9
            color: Qt.tint("#E6E8E7", Qt.rgba(Theme.accentInk.r, Theme.accentInk.g, Theme.accentInk.b, control.progress * 0.92))
            scale: control.hovered && !control.pressed ? 1.04 : 1
            Behavior on scale { enabled: !control.reduced; NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
        }
    }
    contentItem: Item {}
}
