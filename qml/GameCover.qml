import QtQuick
import QtQuick.Controls
import QtQuick.Effects

// Library tile: vertical 2:3 cover with the title and a quiet meta line below.
FocusScope {
    id: root
    required property var game
    property bool selected: false
    property bool reduced: false
    readonly property bool hovered: hit.containsMouse || quickPlay.hovered
    readonly property bool highlighted: hovered || activeFocus
    signal chosen()
    signal previewed(bool active)
    signal editRequested()
    signal playRequested()

    width: 180
    height: width * 1.5 + 58
    activeFocusOnTab: true
    Accessible.role: Accessible.Button
    Accessible.name: game.title + (selected ? qsTr(", selecionado") : "")
    Accessible.onPressAction: chosen()
    onHighlightedChanged: previewed(highlighted)
    Keys.onReturnPressed: chosen()
    Keys.onEnterPressed: chosen()
    Keys.onSpacePressed: chosen()
    Keys.onMenuPressed: editRequested()

    Item {
        id: lifted
        // Above the tile's MouseArea so the quick-play button receives its own clicks.
        z: 1
        width: root.width; height: root.width * 1.5
        transformOrigin: Item.Bottom
        scale: root.highlighted && !root.reduced ? 1.035 : 1
        y: root.highlighted && !root.reduced ? -5 : 0
        Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        Behavior on y { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        // A slight tilt toward the cursor, easing back flat when it leaves.
        readonly property real tiltX: hit.containsMouse && !root.reduced ? (0.5 - hit.mouseY / Math.max(1, root.height)) * 5 : 0
        readonly property real tiltY: hit.containsMouse && !root.reduced ? (hit.mouseX / Math.max(1, root.width) - 0.5) * 6 : 0
        transform: [
            Rotation {
                origin.x: lifted.width / 2; origin.y: lifted.height / 2
                axis { x: 1; y: 0; z: 0 }
                angle: lifted.tiltX
                Behavior on angle { SmoothedAnimation { velocity: 18 } }
            },
            Rotation {
                origin.x: lifted.width / 2; origin.y: lifted.height / 2
                axis { x: 0; y: 1; z: 0 }
                angle: lifted.tiltY
                Behavior on angle { SmoothedAnimation { velocity: 18 } }
            }
        ]

        // Drop shadow, beneath everything else.
        SoftGlow {
            anchors.fill: parent
            color: "black"
            blurMax: 28
            spread: -6
            offsetY: root.highlighted ? 16 : 10
            // Lighter under a selected cover so it doesn't smother the glow.
            opacity: root.selected ? 0.3 : root.highlighted ? 0.8 : 0.55
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
            Behavior on offsetY { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
        }

        // Selection glow in the brand rose, three layers: a wide halo that pools
        // slightly below the cover, a mid body, and a bright rim hugging the edge.
        Item {
            anchors.fill: parent
            opacity: root.selected ? (root.highlighted ? 1 : 0.9) : 0
            scale: root.selected ? 1 : 0.96
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.slow; easing.type: Easing.OutCubic } }
            Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.slow; easing.type: Easing.OutCubic } }
            SoftGlow { anchors.fill: parent; color: Theme.accent; blurMax: 52; spread: 6; offsetY: 10; opacity: root.highlighted ? 0.75 : 0.6 }
            SoftGlow { anchors.fill: parent; color: Theme.accent; blurMax: 22; spread: 3; opacity: 0.8 }
            SoftGlow { anchors.fill: parent; color: Theme.accentHover; blurMax: 7; spread: 3; opacity: 0.75 }
        }

        // Selection ring sits just outside the art, with a dark gap.
        Rectangle {
            anchors.fill: parent; anchors.margins: -5
            radius: Theme.radiusLg + 5
            color: "transparent"
            border.width: 2
            border.color: root.selected ? Theme.accent : Theme.focusRing
            opacity: root.selected || root.activeFocus ? 1 : 0
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.fast } }
        }

        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: Theme.surface
        }
        Image {
            id: art
            anchors.fill: parent
            source: Theme.coverSource(root.game.coverId)
            sourceSize: Qt.size(400, 600)
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: false
        }
        Rectangle { id: mask; anchors.fill: parent; radius: Theme.radiusLg; visible: false; layer.enabled: true }
        MultiEffect {
            anchors.fill: parent
            source: art
            maskEnabled: true
            maskSource: mask
            opacity: art.status === Image.Ready ? 1 : 0
            saturation: root.selected || root.highlighted ? 0 : -0.1
            brightness: root.highlighted ? 0.03 : root.selected ? 0 : -0.06
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
            Behavior on brightness { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
        }
        // Modpacks show their logo on a color taken from it; the cover art stays for the
        // drawer and the hover background.
        Item {
            id: logoTile
            readonly property var saved: launcher.gameSettings[root.game.id] || ({})
            anchors.fill: parent
            visible: !!saved.packIcon && logoImage.status !== Image.Error
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusLg
                color: logoTile.saved.packColor || "#2A2530"
            }
            Rectangle {
                anchors.fill: parent
                radius: Theme.radiusLg
                gradient: Gradient {
                    GradientStop { position: 0; color: "#1AFFFFFF" }
                    GradientStop { position: 0.5; color: "#00FFFFFF" }
                    GradientStop { position: 1; color: "#40000000" }
                }
            }
            Image {
                id: logoImage
                anchors.centerIn: parent
                width: Math.round(parent.width * 0.62); height: width
                source: logoTile.saved.packIcon || ""
                sourceSize: Qt.size(256, 256)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                smooth: true
                mipmap: true
                scale: root.highlighted && !root.reduced ? 1.05 : 1
                Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutCubic } }
            }
        }

        // Bottom shade so the quick-play control stays legible on bright art.
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            opacity: root.highlighted ? 1 : 0
            gradient: Gradient {
                GradientStop { position: 0.55; color: "#00000000" }
                GradientStop { position: 1; color: "#8C000000" }
            }
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.normal } }
        }
        Rectangle {
            anchors.fill: parent
            radius: Theme.radiusLg
            color: "transparent"
            border.width: 1
            border.color: root.highlighted ? "#33FFFFFF" : "#14FFFFFF"
        }

        // Time played, bottom-left on the art.
        Rectangle {
            readonly property int seconds: ((launcher.gameSettings[root.game.id] || {}).playSeconds || 0)
                + (runner.running && runner.gameId === root.game.id ? runner.sessionSeconds : 0)
            visible: seconds >= 60
            anchors.left: parent.left; anchors.bottom: parent.bottom
            anchors.margins: 10
            height: 24
            width: playtimeRow.implicitWidth + 16
            radius: 12
            color: "#A6120F13"
            border.width: 1; border.color: "#1FFFFFFF"
            Row {
                id: playtimeRow
                anchors.centerIn: parent
                spacing: 5
                UiIcon { anchors.verticalCenter: parent.verticalCenter; name: "clock"; width: 12; height: 12; stroke: 1.6; color: Theme.secondary }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: Theme.playtime(parent.parent.seconds)
                    color: Theme.text
                    font.family: Theme.font; font.pixelSize: 11; font.weight: Font.DemiBold
                }
            }
        }

        AbstractButton {
            id: quickPlay
            anchors.right: parent.right; anchors.bottom: parent.bottom
            anchors.margins: 12
            width: 42; height: 42
            hoverEnabled: true
            focusPolicy: Qt.NoFocus
            opacity: root.highlighted ? 1 : 0
            scale: root.highlighted ? 1 : 0.85
            visible: opacity > 0
            Accessible.name: "Jogar " + root.game.title
            onClicked: root.playRequested()
            Behavior on opacity { NumberAnimation { duration: root.reduced ? 0 : Theme.fast } }
            Behavior on scale { NumberAnimation { duration: root.reduced ? 0 : Theme.normal; easing.type: Easing.OutBack } }
            background: Rectangle {
                radius: width / 2
                color: quickPlay.down ? Theme.accentDown : quickPlay.hovered ? Theme.accentHover : Theme.accent
            }
            contentItem: Item {
                UiIcon { anchors.centerIn: parent; anchors.horizontalCenterOffset: 1; width: 17; height: 17; name: "play"; color: Theme.accentInk }
            }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Jogar")
            ToolTip.delay: 500
        }
    }

    Column {
        anchors.top: lifted.bottom; anchors.topMargin: 14
        width: parent.width
        spacing: 3
        Text {
            width: parent.width
            text: root.game.title
            font.family: Theme.font; font.pixelSize: 14
            font.weight: root.selected ? Font.DemiBold : Font.Medium
            color: root.selected || root.highlighted ? Theme.text : Theme.secondary
            elide: Text.ElideRight
            Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }
        }
        Text {
            width: parent.width
            readonly property bool playing: runner.running && runner.gameId === root.game.id
            text: playing ? qsTr("Em execução") : root.game.version + "  ·  " + root.game.loader
            font.family: Theme.font; font.pixelSize: 12
            font.weight: playing ? Font.DemiBold : Font.Normal
            color: playing ? Theme.accentText : Theme.muted
            elide: Text.ElideRight
        }
    }

    MouseArea {
        id: hit
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        onContainsMouseChanged: if (containsMouse) sfx.play("hover")
        onClicked: function(mouse) {
            root.forceActiveFocus(Qt.MouseFocusReason)
            if (mouse.button === Qt.RightButton) root.editRequested()
            else root.chosen()
        }
    }
}
