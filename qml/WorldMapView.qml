import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// One world in 3D (WorldView3D): its chunks built from the game's own block models and
// textures, seen from above at an angle. Drag to move, drag with the right button (or
// Q and E) to turn, wheel to zoom around the cursor, cut it at a height to see caves.
FocusScope {
    id: root
    property bool reduced: false
    property var world: null
    property string dimension: "minecraft:overworld"
    property var info: ({ regions: [], dimensions: [], player: {}, spawn: {}, version: "" })
    property var mobs: []
    property bool showMobs: true
    // A height cut, or `noCut` for none (WorldView3D::NoCut).
    readonly property int noCut: 1000
    property int cut: noCut
    signal closeRequested()

    // The Omni agent's live feed, when the game it runs in has this world open (or is on
    // this server, for a server of this computer); the world files otherwise.
    readonly property bool feed: omniLink.connected && world !== null
                                 && (world.server ? omniLink.world !== "" && omniLink.onLocalServer(world.serverPort)
                                                  : world.gameId === runner.gameId && omniLink.world === world.folder)
    readonly property var player: feed ? omniLink.player : info.player
    readonly property var shownMobs: feed && omniLink.dimension === dimension ? omniLink.entities : mobs
    readonly property bool playerHere: player !== undefined && player.dimension === dimension
    readonly property bool spawnHere: dimension === "minecraft:overworld" && info.spawn && info.spawn.x !== undefined

    function open(newWorld) {
        world = newWorld
        dimension = "minecraft:overworld"
        cut = noCut
        view.yaw = 45
        load(true)
        forceActiveFocus()
    }
    function load(recenter) {
        if (!world) return
        info = worlds.describe(world.path, dimension)
        if (recenter) mobs = []
        worlds.requestEntities(world.path, dimension)
        worlds.watch(world.path, dimension)
        if (recenter) center()
    }
    function close() {
        worlds.watch("", "")
        world = null
    }
    // A game (or this world's server) running: also compare the files' dates now and
    // then (see Worlds::watch). Hidden (another tab), a save only marks it to load again
    // when shown.
    readonly property bool live: world !== null && visible
                                 && (runner.running || (world.server && (servers.status[world.serverId] || {}).state === "running"))
    property bool stale: false
    onLiveChanged: worlds.setPolling(live)
    onVisibleChanged: if (visible && stale) { stale = false; load(false); view.refresh() }
    function center() {
        if (playerHere) view.target = Qt.vector3d(player.x, player.y, player.z)
        else if (spawnHere) view.target = Qt.vector3d(info.spawn.x, info.spawn.y || 64, info.spawn.z)
        else if (info.regions.length > 0) view.target = Qt.vector3d(info.regions[0].x * 512 + 256, 64, info.regions[0].z * 512 + 256)
        else view.target = Qt.vector3d(0, 64, 0)
        view.zoom = 8
    }
    // A quarter turn, to the next corner view.
    function turn(by) {
        turning.to = Math.round((view.yaw - 45) / 90) * 90 + 45 + 90 * by
        turning.restart()
    }
    function dimensionLabel(id) {
        return { "minecraft:overworld": qsTr("Superfície"), "minecraft:the_nether": qsTr("Nether"),
                 "minecraft:the_end": qsTr("The End") }[id] || id
    }

    Keys.onEscapePressed: root.closeRequested()
    Keys.onPressed: function(event) {
        if (event.key === Qt.Key_Q) { root.turn(-1); event.accepted = true }
        else if (event.key === Qt.Key_E) { root.turn(1); event.accepted = true }
    }
    Connections {
        target: worlds
        function onEntitiesLoaded(path, dimension, list) {
            if (root.world && path === root.world.path && dimension === root.dimension) root.mobs = list
        }
        // A save: only the chunks the game wrote are built again, each keeping its old
        // look until the new one is ready.
        function onWorldChanged(path, dimension) {
            if (!root.world || path !== root.world.path || dimension !== root.dimension) return
            if (root.visible) { root.load(false); view.refresh() }
            else root.stale = true
        }
    }

    WorldView3D {
        id: view
        anchors.fill: parent
        worldPath: root.world ? root.world.path : ""
        instance: root.world ? root.world.instance || "" : ""
        dimension: root.dimension
        version: root.info.version || ""
        cut: root.cut
        live: root.feed
        background: Theme.base
        entities: root.showMobs ? root.shownMobs : []
        player: root.playerHere ? root.player : ({})

        NumberAnimation {
            id: turning
            target: view; property: "yaw"
            duration: root.reduced ? 0 : 320; easing.type: Easing.OutCubic
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
            property real lastX: 0
            property real lastY: 0
            onPressed: function(mouse) { lastX = mouse.x; lastY = mouse.y; turning.stop(); root.forceActiveFocus() }
            onPositionChanged: function(mouse) {
                if (pressedButtons & Qt.RightButton) view.yaw -= (mouse.x - lastX) * 0.4
                else view.pan(mouse.x - lastX, mouse.y - lastY)
                lastX = mouse.x; lastY = mouse.y
            }
            onWheel: function(wheel) { view.zoomAt(Math.pow(1.2, wheel.angleDelta.y / 120), wheel.x, wheel.y) }
        }
    }

    Column {
        visible: root.info.regions.length === 0
        anchors.centerIn: parent
        spacing: 8
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.world && !root.world.mappable ? qsTr("Mundo de uma versão antiga") : qsTr("Nada gerado aqui ainda")
            color: Theme.text
            font.family: Theme.fontDisplay; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: root.world && !root.world.mappable ? qsTr("O mapa lê mundos da 1.18 em diante.") : qsTr("Explore essa dimensão no jogo e ela aparece aqui.")
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 13
        }
    }

    // Header
    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right
        height: 120
        gradient: Gradient {
            GradientStop { position: 0; color: "#E6151216" }
            GradientStop { position: 1; color: "#00151216" }
        }
    }
    RowLayout {
        x: 24; y: 44
        width: root.width - 48
        spacing: 12
        ActionButton {
            iconName: "back"; quiet: true; round: true; width: 40; height: 40; reduced: root.reduced
            Accessible.name: qsTr("Voltar")
            onClicked: root.closeRequested()
        }
        ColumnLayout {
            spacing: 1
            Layout.fillWidth: true
            Text {
                text: root.world ? root.world.name : ""
                color: Theme.text
                font.family: Theme.fontDisplay; font.pixelSize: 22; font.weight: Font.DemiBold
                elide: Text.ElideRight; Layout.fillWidth: true
            }
            Row {
                spacing: 8
                Text {
                    text: root.world ? [root.world.server ? qsTr("Servidor") : "", root.world.gameTitle, root.info.version]
                                       .filter(function(part) { return part }).join("  ·  ") : ""
                    color: Theme.muted
                    font.family: Theme.font; font.pixelSize: 12
                }
                Row {
                    visible: root.feed
                    spacing: 5
                    anchors.verticalCenter: parent.verticalCenter
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        anchors.verticalCenter: parent.verticalCenter
                        color: "#7FD36B"
                    }
                    Text {
                        text: qsTr("Ao vivo")
                        color: "#7FD36B"
                        font.family: Theme.font; font.pixelSize: 12; font.weight: Font.DemiBold
                    }
                }
            }
        }
        Row {
            spacing: 6
            Repeater {
                model: root.info.dimensions
                delegate: AbstractButton {
                    id: chip
                    required property string modelData
                    readonly property bool active: root.dimension === modelData
                    height: 32
                    implicitWidth: chipLabel.implicitWidth + 24
                    hoverEnabled: true
                    onClicked: { root.dimension = modelData; root.cut = root.noCut; root.load(true) }
                    background: Rectangle {
                        radius: Theme.radiusSm
                        color: chip.active ? Theme.accentSoft : chip.hovered ? "#14FFFFFF" : "#0AFFFFFF"
                    }
                    contentItem: Text {
                        id: chipLabel
                        text: root.dimensionLabel(chip.modelData)
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                        color: chip.active ? Theme.accentText : Theme.muted
                        font.family: Theme.font; font.pixelSize: 13
                        font.weight: chip.active ? Font.DemiBold : Font.Medium
                    }
                }
            }
        }
        ActionButton {
            text: qsTr("Pasta"); iconName: "folder"; quiet: true; reduced: root.reduced
            onClicked: runner.openFile(root.world.path)
        }
    }

    Rectangle {
        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
        height: 110
        gradient: Gradient {
            GradientStop { position: 0; color: "#00151216" }
            GradientStop { position: 1; color: "#E6151216" }
        }
    }
    // Footer: mobs, height cut, turn, zoom, recentre.
    RowLayout {
        anchors.left: parent.left; anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 20
        spacing: 8
        AbstractButton {
            id: mobsChip
            height: 36
            implicitWidth: mobsLabel.implicitWidth + 24
            hoverEnabled: true
            visible: root.shownMobs.length > 0
            onClicked: root.showMobs = !root.showMobs
            background: Rectangle {
                radius: Theme.radiusSm
                color: root.showMobs ? Theme.accentSoft : mobsChip.hovered ? "#14FFFFFF" : Theme.glassDense
            }
            contentItem: Text {
                id: mobsLabel
                text: qsTr("Mobs") + "  " + root.shownMobs.length
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                color: root.showMobs ? Theme.accentText : Theme.muted
                font.family: Theme.font; font.pixelSize: 13; font.weight: Font.Medium
            }
        }
        Rectangle {
            radius: Theme.radiusSm
            color: Theme.glassDense
            implicitWidth: 300; implicitHeight: 36
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12
                spacing: 10
                Text {
                    text: root.cut === root.noCut ? qsTr("Altura: tudo") : qsTr("Altura: até Y %1").arg(root.cut)
                    color: Theme.secondary
                    font.family: Theme.font; font.pixelSize: 12
                    Layout.preferredWidth: 110
                }
                ValueSlider {
                    Layout.fillWidth: true
                    reduced: root.reduced
                    from: -64; to: 328; stepSize: 8
                    value: root.cut === root.noCut ? 328 : root.cut
                    // Each step builds the chunks in view again; settle on release.
                    live: false
                    onMoved: root.cut = value >= 320 ? root.noCut : value
                }
            }
        }
        Text {
            visible: view.pending > 0
            text: qsTr("Montando %1 chunks…").arg(view.pending)
            color: Theme.muted
            font.family: Theme.font; font.pixelSize: 12
            Layout.leftMargin: 8
        }
        Item { Layout.fillWidth: true }
        ActionButton {
            iconName: "rotateLeft"; round: true; width: 36; height: 36; reduced: root.reduced
            Accessible.name: qsTr("Girar para a esquerda")
            onClicked: root.turn(-1)
        }
        ActionButton {
            iconName: "rotateRight"; round: true; width: 36; height: 36; reduced: root.reduced
            Accessible.name: qsTr("Girar para a direita")
            onClicked: root.turn(1)
        }
        ActionButton {
            iconName: "minus"; round: true; width: 36; height: 36; reduced: root.reduced
            onClicked: view.zoomAt(0.5, view.width / 2, view.height / 2)
        }
        ActionButton {
            iconName: "plus"; round: true; width: 36; height: 36; reduced: root.reduced
            onClicked: view.zoomAt(2, view.width / 2, view.height / 2)
        }
        ActionButton {
            text: root.playerHere ? qsTr("Ir até o jogador") : qsTr("Centralizar"); reduced: root.reduced
            onClicked: root.center()
        }
    }
}
