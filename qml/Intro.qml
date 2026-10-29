import QtQuick
import QtQuick.Effects

// Opening title, about 1.7 s: the logo focuses in out of a blur over a rose
// glow, a sheen of light crosses it, then the veil lifts and the launcher
// settles in underneath (see `reveal`). A click or a key skips it.
Item {
    id: root
    property bool reduced: false
    // Emitted when the veil starts to lift: the launcher animates in under it.
    signal reveal()
    signal finished()
    anchors.fill: parent
    visible: false
    focus: visible

    function play() {
        if (reduced) { finished(); return }
        visible = true
        sequence.start()
    }
    function skip() {
        if (!sequence.running) return
        sequence.stop()
        exit.start()
    }
    Keys.onPressed: function(event) { root.skip(); event.accepted = true }
    MouseArea { anchors.fill: parent; onClicked: root.skip() }

    Rectangle { id: veil; anchors.fill: parent; color: Theme.base }

    SoftGlow {
        id: glow
        width: 340; height: 220
        anchors.centerIn: parent
        radius: 110
        color: Theme.accent
        blurMax: 64
        opacity: 0
        scale: 0.6
    }

    Item {
        id: mark
        anchors.centerIn: parent
        width: Math.round(height * 378 / 120); height: 54
        opacity: 0
        scale: 0.94
        property real haze: 1   // 1 = fully blurred, 0 = sharp

        Image {
            id: logo
            anchors.fill: parent
            source: Theme.brandSource("logo-primary")
            sourceSize.height: 240
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
            visible: false
        }
        MultiEffect {
            anchors.fill: parent
            source: logo
            blurEnabled: true
            blurMax: 48
            blur: mark.haze
        }
        // Sheen: a soft band of light, clipped to the logo's shape.
        Item {
            id: sheen
            anchors.fill: parent
            visible: false
            layer.enabled: true
            property real travel: -0.4
            Rectangle {
                width: parent.width * 0.32; height: parent.height * 3
                anchors.verticalCenter: parent.verticalCenter
                x: parent.width * sheen.travel
                rotation: 18
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: "#00FFFFFF" }
                    GradientStop { position: 0.5; color: "#C8FFFFFF" }
                    GradientStop { position: 1; color: "#00FFFFFF" }
                }
            }
        }
        MultiEffect {
            anchors.fill: parent
            source: sheen
            maskEnabled: true
            maskSource: logoMask
            opacity: 0.55
        }
        Item {
            id: logoMask
            anchors.fill: parent
            visible: false
            layer.enabled: true
            Image { anchors.fill: parent; source: logo.source; sourceSize.height: 240; fillMode: Image.PreserveAspectFit; smooth: true }
        }
    }

    Rectangle {
        id: line
        anchors.horizontalCenter: parent.horizontalCenter
        y: mark.y + mark.height + 26
        width: 0; height: 1
        color: Theme.accent
        opacity: 0
    }

    SequentialAnimation {
        id: sequence
        ScriptAction { script: sfx.play("launch") }
        ParallelAnimation {
            NumberAnimation { target: glow; property: "opacity"; to: 0.3; duration: 900; easing.type: Easing.OutCubic }
            NumberAnimation { target: glow; property: "scale"; to: 1; duration: 1300; easing.type: Easing.OutQuint }
            NumberAnimation { target: mark; property: "opacity"; to: 1; duration: 600; easing.type: Easing.OutCubic }
            NumberAnimation { target: mark; property: "haze"; to: 0; duration: 850; easing.type: Easing.OutCubic }
            NumberAnimation { target: mark; property: "scale"; to: 1; duration: 1200; easing.type: Easing.OutQuint }
            SequentialAnimation {
                PauseAnimation { duration: 380 }
                ParallelAnimation {
                    NumberAnimation { target: line; property: "width"; to: 140; duration: 700; easing.type: Easing.OutQuint }
                    NumberAnimation { target: line; property: "opacity"; to: 0.7; duration: 400 }
                }
            }
            SequentialAnimation {
                PauseAnimation { duration: 520 }
                NumberAnimation { target: sheen; property: "travel"; from: -0.4; to: 1.1; duration: 650; easing.type: Easing.InOutCubic }
            }
        }
        PauseAnimation { duration: 120 }
        ScriptAction { script: exit.start() }
    }

    SequentialAnimation {
        id: exit
        ScriptAction { script: root.reveal() }
        ParallelAnimation {
            NumberAnimation { target: veil; property: "opacity"; to: 0; duration: 650; easing.type: Easing.InOutCubic }
            NumberAnimation { target: mark; property: "opacity"; to: 0; duration: 380; easing.type: Easing.InCubic }
            NumberAnimation { target: mark; property: "scale"; to: 1.05; duration: 650; easing.type: Easing.InCubic }
            NumberAnimation { target: glow; property: "opacity"; to: 0; duration: 500 }
            NumberAnimation { target: line; property: "opacity"; to: 0; duration: 300 }
        }
        ScriptAction { script: { root.visible = false; root.finished() } }
    }
}
