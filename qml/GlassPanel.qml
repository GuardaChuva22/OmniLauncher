import QtQuick
import QtQuick.Effects

// Frosted surface: blurs the region of `source` behind this panel, then tints it.
// `sourceX`/`sourceY` give this panel's top-left in `source` coordinates; they are
// plain bindings so a moving panel (drawer, popup) keeps sampling the right area.
// `source` must not contain this panel, or the capture would be recursive.
Item {
    id: root
    property Item source: null
    property real sourceX: 0
    property real sourceY: 0
    property real radius: Theme.radiusXl
    property color tint: Theme.glass
    property bool shadow: true
    property bool border: true
    property real blurAmount: 1

    // Soft drop shadow from stacked translucent outlines. A MultiEffect shadow
    // leaked its black source into the effect padding below large panels.
    Repeater {
        model: root.shadow ? 8 : 0
        delegate: Rectangle {
            required property int index
            readonly property real spread: (index + 1) * 4
            x: -spread; width: root.width + spread * 2
            y: -spread + 6 + index * 2; height: root.height + spread * 2
            radius: root.radius + spread
            color: "transparent"
            border.width: 4
            border.color: Qt.rgba(0, 0, 0, 0.11 - index * 0.012)
        }
    }
    // Opaque base so the glass never reveals unblurred content underneath.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: Theme.base
        visible: root.source !== null
    }

    ShaderEffectSource {
        id: capture
        // Only reference the source while this panel is actually on screen. Closed
        // popups live outside the window, and a capture held across window
        // teardown crashes on exit.
        sourceItem: root.visible && root.Window.window ? root.source : null
        sourceRect: Qt.rect(root.sourceX, root.sourceY, root.width, root.height)
        // Half resolution is invisible after blurring and keeps the pass cheap.
        textureSize: Qt.size(Math.max(1, Math.ceil(root.width / 2)), Math.max(1, Math.ceil(root.height / 2)))
        live: root.visible && root.opacity > 0
        recursive: false
        visible: false
    }
    // Release the capture before the scene is torn down; otherwise item
    // destruction order on exit can leave the effect pointing at freed items.
    Connections {
        target: Qt.application
        function onAboutToQuit() { capture.sourceItem = null }
    }
    Rectangle {
        id: mask
        anchors.fill: parent
        radius: root.radius
        visible: false
        layer.enabled: true
    }
    MultiEffect {
        visible: root.source !== null
        anchors.fill: parent
        source: capture
        autoPaddingEnabled: false
        blurEnabled: true
        blur: root.blurAmount
        blurMax: 56
        saturation: 0.1
        maskEnabled: true
        maskSource: mask
    }
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        color: root.tint
    }
    // A faint top-down sheen gives the surface its glass light.
    Rectangle {
        anchors.fill: parent
        radius: root.radius
        gradient: Gradient {
            GradientStop { position: 0; color: "#0FFFFFFF" }
            GradientStop { position: 0.28; color: "#00FFFFFF" }
        }
    }
    Rectangle {
        visible: root.border
        anchors.fill: parent
        radius: root.radius
        color: "transparent"
        border.width: 1
        border.color: "#1FFFFFFF"
    }
}
