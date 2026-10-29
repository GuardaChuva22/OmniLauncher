import QtQuick
import QtQuick.Effects

// Round player avatar. The face fills the circle with sharp pixels: an 8×8 face
// image for offline profiles, or the face and hat layers cut from a skin texture.
Rectangle {
    id: root
    property bool empty: false
    // Skin texture (file URL) of a Microsoft profile; empty means the offline face.
    property string skin: ""
    width: 36; height: 36
    radius: width / 2
    color: empty ? "#1AFFFFFF" : "#3D443D"

    UiIcon { anchors.centerIn: parent; visible: root.empty; name: "user"; color: Theme.muted; width: Math.round(root.width * 0.46); height: width }

    Item {
        id: face
        anchors.fill: parent
        visible: false
        layer.enabled: true
        layer.smooth: false
        Image {
            anchors.fill: parent
            visible: root.skin === ""
            source: Theme.offlineFace
            smooth: false
        }
        Image {
            anchors.fill: parent
            visible: root.skin !== ""
            source: root.skin
            sourceClipRect: Qt.rect(8, 8, 8, 8)
            smooth: false
        }
        Image {
            anchors.fill: parent
            visible: root.skin !== ""
            source: root.skin
            sourceClipRect: Qt.rect(40, 8, 8, 8)
            smooth: false
        }
    }
    Rectangle { id: circle; anchors.fill: parent; radius: width / 2; visible: false; layer.enabled: true }
    MultiEffect {
        visible: !root.empty
        anchors.fill: parent
        source: face
        maskEnabled: true
        maskSource: circle
        maskThresholdMin: 0.5
        maskSpreadAtMin: 0.2
    }
}
