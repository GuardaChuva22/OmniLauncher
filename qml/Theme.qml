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

    // "12 min", "2 h 15 min", "34 h"; "" when never played.
    function playtime(seconds) {
        const minutes = Math.floor((seconds || 0) / 60)
        if (seconds > 0 && minutes < 1) return qsTr("menos de 1 min")
        if (minutes < 1) return ""
        if (minutes < 60) return minutes + " min"
        const hours = Math.floor(minutes / 60)
        const rest = minutes % 60
        return hours < 10 && rest > 0 ? hours + " h " + rest + " min" : hours + " h"
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

    // UiIcon outlines (24 × 24 SVG paths), one table for every icon.
    readonly property var iconPaths: ({
        "home": "M3 10L12 3L21 10V20H15V14H9V20H3Z",
        "minus": "M5 12H19",
        "rotateLeft": "M4 12a8 8 0 1 0 2.34-5.66 M4 4V9H9",
        "rotateRight": "M20 12a8 8 0 1 1-2.34-5.66 M20 4V9H15",
        "map": "M3 6L9 3L15 6L21 3V18L15 21L9 18L3 21Z M9 3V18 M15 6V21",
        "grid": "M4 4H10V10H4Z M14 4H20V10H14Z M4 14H10V20H4Z M14 14H20V20H14Z",
        "packs": "M12 3L21 8L12 13L3 8Z M3 12L12 17L21 12 M3 16L12 21L21 16",
        "settings": "M9 3H15L16 6L19 7L22 12L19 17L16 18L15 21H9L8 18L5 17L2 12L5 7L8 6Z M15 12A3 3 0 1 1 9 12A3 3 0 1 1 15 12",
        "plus": "M12 5V19 M5 12H19",
        "search": "M16.5 10.5A6 6 0 1 1 4.5 10.5A6 6 0 1 1 16.5 10.5 M15 15L20 20",
        "chevron": "M9 5L16 12L9 19",
        "down": "M6 9L12 15L18 9",
        "updown": "M8 9L12 5L16 9 M8 15L12 19L16 15",
        "close": "M6 6L18 18 M18 6L6 18",
        "check": "M5 12.5L10 17L19 7",
        "play": "M7.5 4.5L19.5 12L7.5 19.5Z",
        "edit": "M14 5L19 10 M4 20L9 19L20 8L16 4L5 15Z",
        "cube": "M12 2L21 7V17L12 22L3 17V7Z M3 7L12 12L21 7 M12 12V22",
        "back": "M14 5L7 12L14 19 M7 12H20",
        "trash": "M4 7H20 M9 7V4H15V7 M6 7L7 20H17L18 7 M10 11V16 M14 11V16",
        "user": "M16 8A4 4 0 1 1 8 8A4 4 0 1 1 16 8 M4 20C5 15 19 15 20 20",
        "image": "M4 4H20V20H4Z M4 16L9 11L15 17 M13 14L16 11L20 15 M15.5 8H15.51",
        "minimize": "M6 12H18",
        "maximize": "M6 6H18V18H6Z",
        "restore": "M9 5H19V15 M5 9H15V19H5Z",
        "folder": "M3 6H10L12 8H21V19H3Z",
        "dots": "M5 12H5.01 M12 12H12.01 M19 12H19.01",
        "clock": "M20.5 12A8.5 8.5 0 1 1 3.5 12A8.5 8.5 0 1 1 20.5 12 M12 7.5V12L15 14",
        "copy": "M9 9H20V20H9Z M15 9V4H4V15H9",
        "sliders": "M4 7H13 M17 7H20 M15 5V9 M4 12H7 M11 12H20 M9 10V14 M4 17H15 M19 17H20 M17 15V19",
        "info": "M20.5 12A8.5 8.5 0 1 1 3.5 12A8.5 8.5 0 1 1 20.5 12 M12 11V16 M12 8H12.01",
        "puzzle": "M5 8H9V7A2 2 0 0 1 13 7V8H17V12H18A2 2 0 0 1 18 16H17V20H5Z",
        "chip": "M7 7H17V17H7Z M10 3V7 M14 3V7 M10 17V21 M14 17V21 M3 10H7 M3 14H7 M17 10H21 M17 14H21",
        "monitor": "M3 5H21V16H3Z M8 20H16 M12 16V20",
        "terminal": "M4 5H20V19H4Z M8 10L11 12.5L8 15 M13 15H16",
        "coffee": "M5 9H16V14A4 4 0 0 1 12 18H9A4 4 0 0 1 5 14Z M16 10.5H18A2 2 0 0 1 18 14.5H16 M9 3V6 M12.5 3V6",
        "external": "M14 4H20V10 M20 4L11 13 M18 14V20H4V6H10",
        "download": "M12 4V15 M7 10L12 15L17 10 M5 20H19",
        "calendar": "M4 6H20V20H4Z M4 10H20 M8 3V7 M16 3V7",
        "box": "M12 3L20 7.5V16.5L12 21L4 16.5V7.5Z M4 7.5L12 12L20 7.5 M12 12V21",
        "logout": "M10 4H5V20H10 M15 8L19 12L15 16 M19 12H9",
        "server": "M4 4H20V10H4Z M4 14H20V20H4Z M8 7H8.01 M8 17H8.01 M12 7H16 M12 17H16",
        "stop": "M7 7H17V17H7Z",
        "spark": "M12 3L13.8 10.2L21 12L13.8 13.8L12 21L10.2 13.8L3 12L10.2 10.2Z",
        "restart": "M19.5 12A7.5 7.5 0 1 1 17 6.4 M19 3.5V8H14.5"
    })
}
