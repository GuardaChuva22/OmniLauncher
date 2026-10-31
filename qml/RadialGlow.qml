import QtQuick

// A soft ellipse that fades from `color` in the middle to nothing at its edge: a pool of
// light, or a shadow on the floor.
Canvas {
    id: root
    property color color: "black"
    // How far out (0 to 1) the colour holds before it starts to fade.
    property real core: 0.15
    onColorChanged: requestPaint()
    onCoreChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        if (width <= 0 || height <= 0) return
        ctx.save()
        ctx.scale(1, height / width)
        const r = width / 2
        const gradient = ctx.createRadialGradient(r, r, 0, r, r, r)
        const clear = Qt.rgba(root.color.r, root.color.g, root.color.b, 0)
        gradient.addColorStop(0, root.color)
        gradient.addColorStop(root.core, root.color)
        gradient.addColorStop(0.55, Qt.rgba(root.color.r, root.color.g, root.color.b, root.color.a * 0.4))
        gradient.addColorStop(1, clear)
        ctx.fillStyle = gradient
        ctx.fillRect(0, 0, width, width)
        ctx.restore()
    }
}
