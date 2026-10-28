import QtQuick
import QtQuick.Controls

// Modal sheet in the shared glass material. The app blurs and dims everything
// behind it (see the backdrop in Main.qml); the dialog itself re-samples the
// same scene so its surface reads as frosted glass rather than a flat box.
Dialog {
    id: control
    property Item blurSource: null
    property bool reduced: false
    property bool showClose: true
    // True from the start of the enter transition to the start of the exit one,
    // so the backdrop fades together with the dialog instead of after it.
    property bool shown: false
    onAboutToShow: shown = true
    onAboutToHide: shown = false

    anchors.centerIn: parent
    modal: true
    focus: true
    padding: 28
    topPadding: 4
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    // Input is still blocked; the visual backdrop is drawn by Main.qml.
    Overlay.modal: Item {}

    header: Item {
        implicitHeight: 72
        Text {
            x: 28
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width - 100
            text: control.title
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 21; font.weight: Font.DemiBold
            font.letterSpacing: -0.3
            elide: Text.ElideRight
        }
        ActionButton {
            visible: control.showClose
            anchors.right: parent.right; anchors.rightMargin: 18
            anchors.verticalCenter: parent.verticalCenter
            width: 34; height: 34; round: true
            quiet: true; iconName: "close"; iconSize: 15
            reduced: control.reduced
            Accessible.name: "Fechar"
            onClicked: control.close()
        }
    }

    background: GlassPanel {
        source: control.blurSource
        sourceX: control.x
        sourceY: control.y
        tint: Theme.glassDialog
        radius: 24
    }

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: control.reduced ? 0 : 180; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.965; to: 1; duration: control.reduced ? 0 : 260; easing.type: Easing.OutQuint }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; to: 0; duration: control.reduced ? 0 : 120; easing.type: Easing.InCubic }
        NumberAnimation { property: "scale"; to: 0.98; duration: control.reduced ? 0 : 120; easing.type: Easing.InCubic }
    }
}
