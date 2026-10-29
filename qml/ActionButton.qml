import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Button {
    id: control
    property string iconName: ""
    property bool primary: false
    property bool quiet: false
    property bool danger: false
    property bool reduced: false
    property bool centered: true
    property bool round: false
    property real iconSize: height >= 50 ? 20 : 17
    // UI sound on click ("" for none).
    property string sound: "click"
    onClicked: if (sound !== "") sfx.play(sound)
    readonly property bool iconOnly: text === "" && iconName !== ""
    readonly property color ink: primary ? Theme.accentInk : danger ? Theme.danger : Theme.text

    implicitHeight: 40
    implicitWidth: iconOnly ? implicitHeight : contentRow.implicitWidth + (quiet ? 26 : 34)
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    font.family: Theme.font
    font.pixelSize: 14
    font.weight: Font.DemiBold
    Accessible.name: text || iconName
    opacity: enabled ? 1 : 0.38

    background: Rectangle {
        radius: control.round ? height / 2 : Theme.radiusMd
        color: control.danger ? (control.down ? "#4DD26B63" : control.hovered ? "#33D26B63" : (control.quiet ? "transparent" : "#1AD26B63"))
              : control.primary ? (control.down ? Theme.accentDown : control.hovered ? Theme.accentHover : Theme.accent)
              : control.quiet ? (control.down ? Theme.fillActive : control.hovered ? Theme.fillHover : "transparent")
              : control.down ? Theme.fillActive : control.hovered ? Theme.fillHover : Theme.fill
        border.width: control.quiet || control.primary || control.danger ? 0 : 1
        border.color: Theme.line
        Behavior on color { ColorAnimation { duration: control.reduced ? 0 : Theme.fast } }
        // Primary and danger buttons light up softly on hover.
        SoftGlow {
            z: -1
            anchors.fill: parent
            visible: opacity > 0.01
            opacity: !control.enabled ? 0 : control.primary ? (control.down ? 0.75 : control.hovered ? 0.6 : 0)
                   : control.danger && !control.quiet ? (control.hovered ? 0.28 : 0) : 0
            color: control.primary ? Theme.accent : Theme.danger
            radius: parent.radius
            blurMax: 22
            spread: -2
            Behavior on opacity { enabled: !control.reduced; NumberAnimation { duration: Theme.normal; easing.type: Easing.OutCubic } }
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: -3
            radius: parent.radius + 3
            color: "transparent"
            border.width: 2; border.color: Theme.focusRing
            visible: control.visualFocus
        }
    }
    contentItem: Item {
        implicitWidth: contentRow.implicitWidth
        implicitHeight: contentRow.implicitHeight
        RowLayout {
            id: contentRow
            x: control.centered || control.iconOnly ? Math.round((parent.width - width) / 2) : 2
            y: Math.round((parent.height - height) / 2)
            width: implicitWidth; height: implicitHeight
            spacing: 9
            UiIcon {
                visible: control.iconName !== ""
                name: control.iconName
                color: control.ink
                scale: control.hovered && !control.down ? 1.08 : 1
                Behavior on scale { enabled: !control.reduced; NumberAnimation { duration: Theme.fast; easing.type: Easing.OutCubic } }
                Layout.preferredWidth: control.iconSize; Layout.preferredHeight: control.iconSize
            }
            Text {
                visible: control.text !== ""
                text: control.text
                color: control.ink
                font: control.font
                Layout.alignment: Qt.AlignVCenter
            }
        }
    }
    transform: Scale {
        origin.x: control.width / 2; origin.y: control.height / 2
        xScale: control.down ? 0.97 : 1; yScale: xScale
        Behavior on xScale { NumberAnimation { duration: control.reduced ? 0 : 110; easing.type: Easing.OutCubic } }
    }
}
