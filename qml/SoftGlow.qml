import QtQuick
import QtQuick.Effects

// Soft halo or drop shadow for a rounded rectangle the size of this item.
// The shape is drawn into a layer with transparent padding and that layer is
// blurred, so the result fades cleanly to nothing. MultiEffect's built-in
// shadow drew the shape as a hard, unblurred block in this app; don't use it.
Item {
    id: root
    property color color: "black"
    property real radius: Theme.radiusLg
    property real blurMax: 32
    // Grows (or, negative, shrinks) the shape on every side before blurring.
    property real spread: 0
    property real offsetY: 0
    readonly property real pad: blurMax + Math.max(0, spread) + Math.abs(offsetY) + 4

    Item {
        id: shapeLayer
        x: -root.pad; y: -root.pad
        width: root.width + root.pad * 2
        height: root.height + root.pad * 2
        visible: false
        layer.enabled: true
        Rectangle {
            x: root.pad - root.spread
            y: root.pad - root.spread + root.offsetY
            width: root.width + root.spread * 2
            height: root.height + root.spread * 2
            radius: Math.max(0, root.radius + root.spread)
            color: root.color
        }
    }
    MultiEffect {
        x: shapeLayer.x; y: shapeLayer.y
        width: shapeLayer.width; height: shapeLayer.height
        source: shapeLayer
        autoPaddingEnabled: false
        blurEnabled: true
        blur: 1
        blurMax: root.blurMax
    }
}
