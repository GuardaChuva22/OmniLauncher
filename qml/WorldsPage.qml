import QtQuick
import QtQuick.Controls
import QtQuick.Effects
import QtQuick.Layouts

// Every world of every game, read from its level.dat (see Worlds.h).
Item {
    id: root
    property bool reduced: false
    signal notify(string message)
    readonly property real gutter: width < 900 ? 32 : 48
    property var all: []
    property bool loaded: false
    property string gameFilter: ""
    readonly property var shown: gameFilter === "" ? all : all.filter(function(world) { return world.gameId === gameFilter })
    readonly property var gamesWithWorlds: {
        const ids = []
        for (const world of all) if (ids.indexOf(world.gameId) === -1) ids.push(world.gameId)
        return launcher.games.filter(function(game) { return ids.indexOf(game.id) !== -1 })
    }

    function refresh() { worlds.request(launcher.games) }
    function modeLabel(world) {
        if (world.hardcore) return qsTr("Hardcore")
        return { survival: qsTr("Sobrevivência"), creative: qsTr("Criativo"),
                 adventure: qsTr("Aventura"), spectator: qsTr("Espectador") }[world.mode] || ""
    }
    function when(ms) {
        return ms > 0 ? new Date(ms).toLocaleString(Qt.locale(), qsTr("d 'de' MMMM yyyy, HH:mm")) : ""
    }
    onVisibleChanged: if (visible) refresh()
    Connections {
        target: worlds
        function onListed(list) { root.all = list; root.loaded = true }
    }
    Connections {
        target: runner
        function onGameExited() { if (root.visible) root.refresh() }
    }

    RowLayout {
        id: header
        x: root.gutter; y: 50
        width: root.width - root.gutter * 2
        spacing: 12
        Row {
            spacing: 12
            Layout.fillWidth: true
            Text {
                id: titleText
                text: qsTr("Mundos")
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 30; font.weight: Font.DemiBold
                font.letterSpacing: -0.6
            }
            Text {
                anchors.baseline: titleText.baseline
                text: root.shown.length
                color: Theme.faint
                font.family: Theme.fontDisplay; font.pixelSize: 20; font.weight: Font.Medium
            }
        }
    }

    Row {
        id: filters
        x: root.gutter; y: header.y + header.height + 18
        spacing: 6
        visible: root.all.length > 0
        Repeater {
            model: [{ id: "", title: qsTr("Todos") }].concat(root.gamesWithWorlds.map(function(g) { return { id: g.id, title: g.title } }))
            delegate: AbstractButton {
                id: chip
                required property var modelData
                readonly property bool active: root.gameFilter === modelData.id
                height: 32
                implicitWidth: chipLabel.implicitWidth + 24
                hoverEnabled: true
                onClicked: root.gameFilter = modelData.id
                background: Rectangle {
                    radius: Theme.radiusSm
                    color: chip.active ? Theme.accentSoft : chip.hovered ? "#0AFFFFFF" : "transparent"
                }
                contentItem: Text {
                    id: chipLabel
                    text: chip.modelData.title
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                    color: chip.active ? Theme.accentText : Theme.muted
                    font.family: Theme.font; font.pixelSize: 13
                    font.weight: chip.active ? Font.DemiBold : Font.Medium
                }
            }
        }
    }

    GridView {
        id: grid
        x: root.gutter - 8
        y: filters.y + filters.height + 16
        width: root.width - root.gutter * 2 + 16
        height: root.height - y
        readonly property int columns: Math.max(1, Math.floor(width / 360))
        cellWidth: Math.floor(width / columns)
        cellHeight: 104
        clip: true
        bottomMargin: 32
        model: root.shown
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ThinScrollBar {}
        delegate: Item {
            id: cell
            required property var modelData
            width: grid.cellWidth; height: grid.cellHeight
            Rectangle {
                id: card
                anchors.fill: parent; anchors.margins: 8
                radius: Theme.radiusLg
                color: hover.hovered ? "#0AFFFFFF" : Theme.fill
                border.width: 1; border.color: hover.hovered ? "#33FFFFFF" : "#14FFFFFF"
                Behavior on color { ColorAnimation { duration: root.reduced ? 0 : Theme.fast } }

                Item {
                    id: thumb
                    x: 12; anchors.verticalCenter: parent.verticalCenter
                    width: 64; height: 64
                    Image {
                        id: icon
                        anchors.fill: parent
                        source: cell.modelData.icon
                        sourceSize: Qt.size(128, 128)
                        smooth: false
                        asynchronous: true
                        cache: false
                        visible: false
                    }
                    Rectangle { id: iconMask; anchors.fill: parent; radius: Theme.radiusSm; visible: false; layer.enabled: true }
                    Rectangle {
                        anchors.fill: parent; radius: Theme.radiusSm
                        color: "#0DFFFFFF"
                        UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "map"; color: Theme.faint; visible: icon.status !== Image.Ready }
                    }
                    MultiEffect {
                        anchors.fill: parent; source: icon; maskEnabled: true; maskSource: iconMask
                        visible: icon.status === Image.Ready
                    }
                }

                ColumnLayout {
                    anchors.left: thumb.right; anchors.leftMargin: 14
                    anchors.right: parent.right; anchors.rightMargin: 14
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: cell.modelData.name
                        elide: Text.ElideRight
                        color: Theme.text
                        font.family: Theme.font; font.pixelSize: 15; font.weight: Font.DemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: [cell.modelData.server ? qsTr("Servidor") : "", cell.modelData.gameTitle, cell.modelData.version,
                               root.modeLabel(cell.modelData)]
                              .filter(function(part) { return part }).join("  ·  ")
                        elide: Text.ElideRight
                        color: cell.modelData.hardcore ? Theme.danger : Theme.muted
                        font.family: Theme.font; font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true
                        text: root.when(cell.modelData.lastPlayed)
                        elide: Text.ElideRight
                        color: Theme.faint
                        font.family: Theme.font; font.pixelSize: 11
                    }
                }
                HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: mapView.open(cell.modelData) }
                ToolTip.visible: hover.hovered && cell.modelData.seed !== ""
                ToolTip.delay: 600
                ToolTip.text: qsTr("Semente: ") + cell.modelData.seed
            }
        }
    }

    Column {
        visible: root.loaded && root.all.length === 0
        anchors.centerIn: parent
        spacing: 10
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: 60; height: 60; radius: 30
            color: Theme.fill; border.width: 1; border.color: Theme.line
            UiIcon { anchors.centerIn: parent; width: 26; height: 26; name: "map"; color: Theme.muted }
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Nenhum mundo ainda")
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: qsTr("Crie um mundo em qualquer jogo e ele aparece aqui.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }

    WorldMapView {
        id: mapView
        anchors.fill: parent
        visible: world !== null
        reduced: root.reduced
        onCloseRequested: close()
    }
}
