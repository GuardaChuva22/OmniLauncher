import QtQuick
import QtQuick.Shapes

Item {
    id: root
    property string name: "plus"
    property color color: Theme.text
    property real stroke: 1.7
    readonly property bool dotted: name === "dots"
    width: 20; height: 20
    Shape {
        width: 24; height: 24
        scale: root.width / 24
        transformOrigin: Item.TopLeft
        preferredRendererType: Shape.CurveRenderer
        ShapePath {
            strokeColor: root.color
            // Keep the rendered stroke near `stroke` px at any icon size.
            strokeWidth: root.dotted ? 3.4 : root.stroke * 24 / Math.max(1, root.width)
            capStyle: ShapePath.RoundCap
            joinStyle: ShapePath.RoundJoin
            fillColor: root.name === "play" ? root.color : "transparent"
            PathSvg { path: Theme.iconPaths[root.name] || Theme.iconPaths.plus }
        }
    }
}
