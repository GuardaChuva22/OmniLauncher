pragma Singleton
import QtQuick

// Palette from the Omni "Modular v01" identity (assets/ARTWORK.md):
// rose #E68AA6 · rose light #F3B7CB · graphite #151216 · surface #211D23
// porcelain #F5F1ED · secondary #B0A7AF. Rose is reserved for the brand,
// selection, focus and the primary action; covers supply the other colors.
QtObject {
    // Text
    readonly property color text: "#F5F1ED"
    readonly property color secondary: "#D3CCD1"
    readonly property color muted: "#B0A7AF"
    readonly property color faint: "#7B7279"
    // Semantic colors stay independent from the brand color.
    readonly property color danger: "#F0A19A"
    readonly property color success: "#A9CFA0"

    // Base and materials: the warm graphite family at different densities, so
    // sidebar, drawer, menus and dialogs read as one material.
    readonly property color base: "#151216"
    readonly property color surface: "#211D23"
    readonly property color sidebar: "#B3120F13"
    readonly property color glass: "#B8151216"
    readonly property color glassDense: "#D6181519"
    readonly property color glassDialog: "#CC1B171D"
    readonly property color menu: "#D1211D23"
    readonly property color popup: "#F5211D23"
    readonly property color scrim: "#59080608"
    readonly property color backdropTint: "#66090709"

    // Neutral fills on top of materials
    readonly property color fill: "#0CFFFFFF"
    readonly property color fillHover: "#14FFFFFF"
    readonly property color fillActive: "#1FFFFFFF"
    readonly property color line: "#12FFFFFF"
    readonly property color lineStrong: "#24FFFFFF"

    // Brand rose: primary action, selection, focus, active states
    readonly property color accent: "#E68AA6"
    readonly property color accentHover: "#F3B7CB"
    readonly property color accentDown: "#D7768F"
    readonly property color accentInk: "#151216"
    readonly property color accentText: "#F3B7CB"
    readonly property color accentSoft: "#26E68AA6"
    readonly property color accentSoftHover: "#33E68AA6"
    readonly property color accentLine: "#99E68AA6"
    readonly property color focusRing: "#F3B7CB"

    // Shape
    readonly property int radiusSm: 8
    readonly property int radiusMd: 10
    readonly property int radiusLg: 14
    readonly property int radiusXl: 20

    // Motion (ms)
    readonly property int fast: 140
    readonly property int normal: 220
    readonly property int slow: 360

    // Type: Manrope (OFL), bundled in assets/fonts and registered in main.cpp.
    readonly property string font: "Manrope"
    readonly property string fontDisplay: "Manrope"

    readonly property var covers: ["meadow", "copper", "dusk", "frost"]
    readonly property var loaders: ["Vanilla", "Fabric", "Forge", "NeoForge"]
    readonly property var versions: ["1.21.1", "1.20.4", "1.20.1", "1.19.2"]

    // Offline avatar face, chosen once per launch: Herobrine every 31 October,
    // otherwise a 1 in 10,000 chance of the Omni Steve.
    readonly property url offlineFace: {
        const today = new Date()
        const name = today.getMonth() === 9 && today.getDate() === 31 ? "herobrine_face"
            : Math.random() < 0.0001 ? "omnisteve_face" : "steve_face"
        return Qt.resolvedUrl("../assets/avatars/" + name + ".png")
    }

    function brandSource(name) { return Qt.resolvedUrl("../assets/brand/" + name + ".png") }
    function coverSource(id) {
        if (!id) return Qt.resolvedUrl("../assets/covers/meadow.png")
        if (id.indexOf("file:") === 0 || id.indexOf("qrc:") === 0) return id
        return Qt.resolvedUrl("../assets/covers/" + id + ".png")
    }
    // One ambient color per bundled cover, used for atmosphere details only;
    // imported images use a neutral warm tone.
    function coverTint(id) {
        return ({ meadow: "#8CC37A", copper: "#DE9A55", dusk: "#B993DE", frost: "#78C6D8" })[id] || "#CFC8BA"
    }
}
